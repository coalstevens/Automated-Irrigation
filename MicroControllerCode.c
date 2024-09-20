#include <msp430.h> 

#define DATASET_SIZE 600        //Size of the FRAM dataset to store lightdata in
#define WATER_THRESHOLD 770     //Soil sensor read threshold to indicate when to water plants

//State is used to track which sensor to read from next. SS = Soil Sensor
enum state {readLight, readSS0, readSS1, readSS2, readSS3};
volatile enum state myState;

volatile unsigned int bitTrick[] = {1, 2, 4, 8};       //Used in waterPlants() to select motors
volatile unsigned int soilSensorData[4];               //Store Soil Data for packets
volatile unsigned int lightTemp;                       //Store light Data for packets
volatile unsigned int escape;                          //Escape "byte"
volatile unsigned int count[4];                        //Pump Timers
volatile unsigned int adcCount;                        //Count how many times the adc has been read from
volatile unsigned int dataCount;                       //Count waiting for 5 minutes to upload light sensor data
volatile unsigned int exportIndex;                      //exportIndex is used to track how much data has been trasmitted so far during export

//FRAM Variables
//These variables are not reset when program is initialized.
#pragma PERSISTENT(lightData)
static unsigned int lightData[DATASET_SIZE] = {0};        //Array to store the light data
#pragma PERSISTENT(startIndicator)
static unsigned int startIndicator[DATASET_SIZE] = {0};   //Array to store the lightIndicator, which tells you the last updated value of lightData
#pragma PERSISTENT(index)
volatile unsigned int index = 0;                        //Track the next index to store light data
#pragma PERSISTENT(dataEnd)
volatile unsigned int dataEnd = 1;                      //Current flag to set the lightIndicator value
#pragma PERSISTENT(dayCounter)
volatile unsigned long int dayCounter = 0;              //dayCounter is for fertilizeLEDS(). Used to count how many days have passed since fertilizing

//Function declarations
void makeEscape();
void waterPlants();
void fertilizeLEDS();

int main(void)
{
    int i;                      //loop index

    WDTCTL = WDTPW | WDTHOLD;   // stop watchdog timer


    myState = readLight;        //Initialize state to read from the light sensor first

    //Initialize non-FRAM counters at 0
    dataCount = 0;
    adcCount = 0;
    for (i = 0; i < 4; i++)
        count[i] = 0;

    // Set P2.7 output to high to power ADC
    P2DIR |= BIT7;
    P2OUT |= BIT7;

    // Clock configuration
    CSCTL0 = CSKEY;                     // Set clock access key
    CSCTL1 |= DCOFSEL_3;                // DCO = 8 MHz
    CSCTL2 = SELS_3 + SELM_3 + SELA_3;  // SMCLK = DCO, MCLK = DCO, ACLK = VLOCLK 1 (10kHz)
    CSCTL3 = DIVA_5;                    // Set ACLK divider to 1/32

    //Timer A0 configured to 25Hz CCR0 interrupt, currently not enabled
    TA0CTL = MC_1 + TASSEL_1;                  //Select ACLK, set up mode, set input divider to /8
    TA0CCR0 = 10000;                           //Set CCR0 at 25 Hz (8000000/32/25)

    //Timer B0 configured to 5Hz CCR0 interrupt
    TB0CTL = MC_1 + TBSSEL_1;                  //Set up mode, select ACLK
    TB0CCTL0 = CCIE;                           //Enable Interrupt
    TB0CCR0 = 50000;                           //Set CCR0 at 5 Hz (8000000/32/5)

    REFCTL0 = REFVSEL_2;                       //Configure VRef to 2.5V

    //ADC Configuration
    ADC10CTL0 &= ~ADC10ENC;                      //Disable ADC
    ADC10CTL0 |= ADC10ON;                        //Turn ADC On
    ADC10CTL1 |= ADC10SHP;                       //Source from sampling timer
    ADC10CTL2 |= ADC10RES;                       //Set to 10 bit resolution
    ADC10IE |= ADC10IE0;                         //Enable interrupts on conversion complete
    ADC10MCTL0 |= ADC10SREF_1 + ADC10INCH_0;     //Initialize to Light Sensor settings. A5 and VREF(2.5V)-GND

    //Configure UART
    UCA0CTLW0 = UCSSEL_3;                       //Select SMCLK running at 8MHz
    UCA0BRW = 52;                               //Configure for 9600 Baud
    UCA0MCTLW = UCOS16 + UCBRF0 + 0x4900;       //  "           "       "

    // Configure P2.0 & P2.1 for UCA0
    P2SEL0 &= ~BIT0 + ~BIT1;
    P2SEL1 |= BIT0 + BIT1;

    //Configuring P1.0, P1.1, P1.2, P1.3, P1.5 as ADC channel inputs (A0, A1, A2, A3, A5)
    //Pin 1.4 is wonky for some reason so it is not used. It adds ~200 to sensor input
    P1SEL1 = BIT0 + BIT1 + BIT2 + BIT3 + BIT5;
    P1SEL0 = BIT0 + BIT1 + BIT2 + BIT3 + BIT5;

    //Configuring P3.0-P3.3 to control pumps via the relay.
    P3DIR |= BIT0 + BIT1 + BIT2 + BIT3;              //Configure 3.0 for output
    P3OUT |= BIT0 + BIT1 + BIT2 + BIT3;              //Set output to high

    //Setting up P4.0 and P4.1 for Switch Control
    P4DIR &= ~BIT0 + ~BIT1;         //Setting up I/O configuration for input with pullup resistor at P4.0
    P4OUT |= BIT0 + BIT1;           //      "           "           "           "           "           "
    P4REN |= BIT0 + BIT1;           //      "           "           "           "           "           "
    P4IE |= BIT0 + BIT1;            //Interrupt enabled
    P4IES |= BIT0 + BIT1;           //Set to be interrupted at falling edge

    P3DIR |= BIT4 + BIT5 + BIT6 + BIT7;     //Enable P3.4 through P3.7 for LED output
    PJDIR |= BIT0 + BIT1 + BIT2 + BIT3;     //Enable PJ.0 through PJ.3 for  LED output

    P3OUT &= ~(BIT4 + BIT5 + BIT6 + BIT7);  //Make sure LEDS are off
    PJOUT &= ~(BIT0 + BIT1 + BIT2 + BIT3);  //Make sure LEDS are off

    _EINT();                //Enable Global Interrupts

    while(1);

    return 0;
}

//Port 4 (Switch) Interrupt Service Routine
#pragma vector = PORT4_VECTOR
__interrupt void Port4(void)
{
    unsigned long int i;

    switch(__even_in_range(P4IV,0x10))
        {
    //On switch P4.0 press, reset the LEDS and bring the dayCounter to 0
    //Switch should be pressed once the plants have been fertilized
        case P4IV_P4IFG0:
            P3OUT &= ~(BIT4 + BIT5 + BIT6 + BIT7);             //Turn off P3 LEDS
            PJOUT &= ~(BIT0 + BIT1 + BIT2 + BIT3);             //Turn off PJ LEDS
            dayCounter = 0;
            break;

    //On switch P4.1 press, stop transmitting 1Hz sensor data, and
    //transmit the light sensor dataSet through the serial port
        case P4IV_P4IFG1:
            exportIndex = 0;
            TB0CCTL0 &= ~CCIE;      //Disable TA0 CC0 Interrupt

            //Wait a few seconds before beginning export
            for (i = 0; i < 5000000; i++)
                _NOP();

            TA0CCTL0 = CCIE;      //Enable TB0 CC0 interrupt
            break;
        }
}

//This interrupt vector is triggered by the P4.1 switch
//This vector is used to transmit all of the data contained within the lightData array,
//as well as the corresponding startIndicator element.
//Once all the data has been exported, the vector restarts the TB0 interrupt and shuts down
//the TA0 timer interrupt
#pragma vector = TIMER0_A0_VECTOR
__interrupt void TIMER0_A0_ISR (void)
{
    unsigned long int i;

    while ((UCA0IFG & UCTXIFG)==0);
    UCA0TXBUF = lightData[exportIndex] >> 8;                //Send upper byte
    while ((UCA0IFG & UCTXIFG)==0);
    UCA0TXBUF = lightData[exportIndex];                     //Send lower byte
    while ((UCA0IFG & UCTXIFG)==0);
    UCA0TXBUF = startIndicator[exportIndex];                //Send startIndicator value

    exportIndex++;
    if (exportIndex >= DATASET_SIZE) {
        TA0CCTL0 &= ~CCIE;     //Disable TB0 CC0 interrupt

        //Wait a few seconds before ending export
        for (i = 0; i < 5000000; i++)
            _NOP();

        TB0CCTL0 |= CCIE;      //Enable TA0 CC0 Interrupt
    }
}

#pragma vector = ADC10_VECTOR
__interrupt void ADC10_ISR (void)
{
    if(ADC10IV == 12) {
        ADC10CTL0 &= ~ADC10ENC;         //Disable ADC so I can change ADC settings

        switch(myState) {
        case readLight:

            lightTemp = ADC10MEM0;

            //Every 5 minutes add a light sensor output into the data array
            if (dataCount >= 300) {
                dataCount = 0;                          //Reset 5 minute counter
                lightData[index] = lightTemp;           //Store data from light sensor to FRAM array
                startIndicator[index] = dataEnd;        //Store flag data to FRAM array to track where the last data point occcured

                lightTemp = lightData[index];           //Store data from light sensor for packet stream
                index++;                                //Move to next value in the arrays

                if (index >= DATASET_SIZE) {
                    index = 0;                          //Reset index so index doesn't exceed array size
                    dataEnd = !dataEnd;                 //Flip boolean
                }
            }

            myState = readSS0;                          //Read SS0 next ADC cycle
            ADC10MCTL0 = ADC10SREF_0 + ADC10INCH_1;     //Set ADC reference to VCC-GND, select A1 for SS0
            break;

        case readSS0:
            soilSensorData[0] = ADC10MEM0;

            myState = readSS1;                          //Read SS1 next ADC cycle
            ADC10MCTL0 = ADC10SREF_0 + ADC10INCH_2;     //Set ADC reference to VCC-GND, select A2 for SS1
            break;

        case readSS1:
            soilSensorData[1] = ADC10MEM0;

            myState = readSS2;                          //Read SS2 next ADC cycle
            ADC10MCTL0 = ADC10SREF_0 + ADC10INCH_3;     //Set ADC reference to VCC-GND, select A3 for SS2
            break;

        case readSS2:
            soilSensorData[2] = ADC10MEM0;

            myState = readSS3;                          //Read SS3 next ADC cycle
            ADC10MCTL0 = ADC10SREF_0 + ADC10INCH_5;     //Set ADC reference to VCC-GND, select A4 for SS3
            break;

        case readSS3:
            soilSensorData[3] = ADC10MEM0;

            myState = readLight;                        //Read Light Sensor next ADC cycle
            ADC10MCTL0 = ADC10SREF_1 + ADC10INCH_0;     //Set ADC reference to VREF(2.5V)-GND, select A0 for light sensor
            break;
        }
    }
}

//This TB0 vector triggers the ADC to read from the 5 sensors
//and transmits the data packet at 1Hz.
//The data packet contains the start byte, the light sensor output,
//the four soil sensor output, and the escape byte
#pragma vector = TIMER0_B0_VECTOR
__interrupt void TIMER0_B0_ISR (void)
{
    ADC10CTL0 |= ADC10ENC + ADC10SC;            //Start sample and conversion
    while(ADC10CTL1 & BUSY);                    //Wait for conversion complete
    adcCount++;                                 //Track how many sensor have been read from

    //When the 5 sensors have updated, send the datapacket at 1Hz.
    if (adcCount >= 5) {
        adcCount = 0;
        makeEscape();

        //Send Start Byte
        while ((UCA0IFG & UCTXIFG)==0);
        UCA0TXBUF = 255;

        //Send Light Sensor data
        while ((UCA0IFG & UCTXIFG)==0);
        UCA0TXBUF = lightTemp >> 8;
        //UCA0TXBUF = 0;
        while ((UCA0IFG & UCTXIFG)==0);
        UCA0TXBUF = lightTemp;
        //UCA0TXBUF = 0;

        //Send SS0 data
        while ((UCA0IFG & UCTXIFG)==0);
        UCA0TXBUF = soilSensorData[0] >> 8;
        while ((UCA0IFG & UCTXIFG)==0);
        UCA0TXBUF = soilSensorData[0];

        //Send SS1 data
        while ((UCA0IFG & UCTXIFG)==0);
        UCA0TXBUF = soilSensorData[1] >> 8;
        while ((UCA0IFG & UCTXIFG)==0);
        UCA0TXBUF = soilSensorData[1];

        //Send SS2 data
        while ((UCA0IFG & UCTXIFG)==0);
        UCA0TXBUF = soilSensorData[2] >> 8;
        while ((UCA0IFG & UCTXIFG)==0);
        UCA0TXBUF = soilSensorData[2];

        //Send SS3 data
        while ((UCA0IFG & UCTXIFG)==0);
        UCA0TXBUF = soilSensorData[3] >> 8;
        while ((UCA0IFG & UCTXIFG)==0);
        UCA0TXBUF = soilSensorData[3];

        //Send escape "byte"
        while ((UCA0IFG & UCTXIFG)==0);
        UCA0TXBUF = escape >> 8;
        while ((UCA0IFG & UCTXIFG)==0);
        UCA0TXBUF = escape;

        dataCount++;                    //Increment 5 minute counter for FRAM light sensor dataSet

        if (dayCounter < 2506000)       //Increment the dayCounter, if the count goes beyond 29 days stop counting.
            dayCounter++;

        //Interrupts are disabled to prevent the pumps from getting stuck on
        _DINT();                //Disable interrupts
        fertilizeLEDS();        //Execute fertilizer light protocol
        waterPlants();          //Execute watering protocol
        _EINT();                //Enable interrupts
    }
}

//Turn on the next LED on the 8 days before the 30th day.
//Plants should be fertilized every 30 days-ish.
void fertilizeLEDS() {
    //Values currently set to seconds instead of days for demonstration purposes.
    switch(dayCounter) {
    case 10:                       //Value should be 1900800 for 22nd day
        PJOUT = BIT0;
        P3OUT = 0;
        break;
    case 20:                            //Value should be 1987200 for 23rd day
        PJOUT = BIT0 + BIT1;
        P3OUT = 0;
        break;
    case 30:                            //Value should be 2073600 for 24th day
        PJOUT = BIT0 + BIT1 + BIT2;
        P3OUT = 0;
        break;
    case 40:                            //Value should be 2160000 for 25th day
        PJOUT = BIT0 + BIT1 + BIT2 + BIT3;
        P3OUT = 0;
        break;
    case 50:                            //Value should be 2246400 for 26th day
        PJOUT = BIT0 + BIT1 + BIT2 + BIT3;
        P3OUT = BIT4;
        break;
    case 60:                            //Value should be 2332800 for 27th day
        PJOUT = BIT0 + BIT1 + BIT2 + BIT3;
        P3OUT = BIT4 + BIT5;
        break;
    case 70:                            //Value should be 2419200 for 28th day
        PJOUT = BIT0 + BIT1 + BIT2 + BIT3;
        P3OUT = BIT4 + BIT5 + BIT6;
        break;
    case 80:                            //Value should be 2505600 for 29th day
        PJOUT = BIT0 + BIT1 + BIT2 + BIT3;
        P3OUT = BIT4 + BIT5 + BIT6 + BIT7;
        break;
    default:
        break;
    }
}

//For each of the Soil Sensor and Pump combinations:
//If a sensor reads a value greater than the watering threshold 5 times (~5 seconds)
//then it turns on the corresponding pump for 3 seconds.
//Then, do not water for at least 60 seconds, giving time for the water
//to drain through the soil.
void waterPlants(void) {
    unsigned int i;

    for (i = 0; i < 4; i++) {
        if (soilSensorData[i] > WATER_THRESHOLD || count[i] >= 5) {
            if (count[i] == 5)
                P3OUT &= ~bitTrick[i];                      //Turn on pump
            else if (count[i] >= 6 && count[i] < 68)
                P3OUT |= bitTrick[i];                       //Turn off pump
            else if (count[i] >= 68)
                count[i] = 0;
            count[i]++;
        }
        else {
            //if ((P3OUT & bitTrick[i]) != 0)
                P3OUT |= bitTrick[i];                       //If the pump isn't off already, turn it off
            count[i] = 0;
        }
    }
}

//Creates a 10-bit escape "byte" to represent 255 values for the 10 databytes in the data packet
void makeEscape(void) {
    //escape[9] = lightTemp High
    //escape[8] = lightTemp Low
    //escape[7] = SS0 High
    //escape[6] = SS0 Low
    //escape[5] = SS1 High
    //escape[4] = SS1 Low
    //escape[3] = SS2 High
    //escape[2] = SS2 Low
    //escape[1] = SS3 High
    //escape[0] = SS3 Low

    //Reset escape bytes
    escape = 0;

    //LIGHT BYTES
    if ((lightTemp >> 8 ) == 255) {             //If highbyte == 255
        lightTemp &= ~(0xFF00);                 //highbyte = 0;
        escape |= 0b1000000000;                 //Set corresponding escape bit
    }
    if ((lightTemp & 0x00FF ) == 255) {         //If lowbyte == 255
        lightTemp &= ~(0x00FF);                 //lowbyte = 0
        escape |= 0b0100000000;                 //Set corresponding secape bit
    }

    //SS0 BYTES
    if ((soilSensorData[0] >> 8 ) == 255) {         //If highbyte == 255
        soilSensorData[0] &= ~(0xFF00);             //highbyte = 0;
        escape |= 0b0010000000;                     //Set corresponding escape bit
    }
    if ((soilSensorData[0] & 0x00FF ) == 255) {     //If lowbyte == 255
        soilSensorData[0] &= ~(0x00FF);             //lowbyte = 0
        escape |= 0b0001000000;                     //Set corresponding escape bit
    }

    //SS1 BYTES
    if ((soilSensorData[1] >> 8 ) == 255) {         //If highbyte == 255
        soilSensorData[1] &= ~(0xFF00);             //highbyte = 0;
        escape |= 0b0000100000;                     //Set corresponding escape bit
    }
    if ((soilSensorData[1] & 0x00FF ) == 255) {     //If lowbyte == 255
        soilSensorData[1] &= ~(0x00FF);             //lowbyte = 0
        escape |= 0b0000010000;                     //Set corresponding escape bit
    }

    //SS2 BYTES
    if ((soilSensorData[2] >> 8 ) == 255) {         //If highbyte == 255
        soilSensorData[2] &= ~(0xFF00);             //highbyte = 0;
        escape |= 0b0000001000;                     //Set corresponding escape bit
    }
    if ((soilSensorData[2] & 0x00FF ) == 255) {     //If lowbyte == 255
        soilSensorData[2] &= ~(0x00FF);             //lowbyte = 0
        escape |= 0b0000000100;                     //Set corresponding escape bit
    }

    //SS3 BYTES
    if ((soilSensorData[3] >> 8 ) == 255) {         //If highbyte == 255
        soilSensorData[3] &= ~(0xFF00);             //highbyte = 0;
        escape |= 0b0000000010;                     //Set corresponding escape bit
    }
    if ((soilSensorData[3] & 0x00FF ) == 255) {     //If lowbyte == 255
        soilSensorData[3] &= ~(0x00FF);             //lowbyte = 0
        escape |= 0b0000000001;                     //Set corresponding escape bit
    }
}
