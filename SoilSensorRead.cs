//Cole Stevens (51015162)
//MECH 423 Project
//November 23rd, 2020

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Text;
using System.Windows.Forms;

namespace MECH423_Lab2_Exam_Serial_Comm
{
    public partial class SoilSensorRead : Form
    {
        public int numberOfDataPoints = 0;
        StreamWriter outputFile;
        
        //States are used to track the input bytes from the serial port
        public enum state {start, lightHigh, lightLow, SS0High, SS0Low, 
                           SS1High, SS1Low, SS2High, SS2Low, SS3High, 
                           SS3Low, escapeHigh, escapeLow};
        public enum exportState { lightHigh, lightLow, startIndicator };
        state myState = state.start;
        exportState myExportState = exportState.lightHigh;

        Int32[] soilSensorData = new Int32[4];      //To store soil sensor data
        Int32 lightData;                            //To store light sensor data
        Int32 escape;                               //To store the escape byte
        readonly Int32 DATASET_SIZE = 600;          //The size of the dataSet
        Int32 exportData;                           //The light data to export to the output file
        Int32 exportCount = 0;                      //Track how much data has been exported
        Int32 startIndicator;                       //To store the start indicator data in the dataset

        //Declaring a queue to store incoming data
        ConcurrentQueue<Int32> dataQueue = new ConcurrentQueue<Int32>();

        public SoilSensorRead()
        {
            InitializeComponent();
        }

        //Determine com ports and update the combobox accordingly
        public void ComPortUpdate()
        {
            cmbComPort.Items.Clear();
            string[] comPortArray = System.IO.Ports.SerialPort.GetPortNames().ToArray();
            Array.Reverse(comPortArray);
            cmbComPort.Items.AddRange(comPortArray);
            if (cmbComPort.Items.Count != 0)
                cmbComPort.SelectedIndex = 0;
            else
                cmbComPort.Text = "No Ports Found!";
        }

        private void Form1_Load(object sender, EventArgs e)
        {
            ComPortUpdate();
        }
        
        //The connect button tries to establish a connection
        //to the serial port selected in the combobox
        //at 9600 Baud
        private void btnConnect_Click(object sender, EventArgs e)
        {
            if (btnConnect.Text == "Connect")
            {
                if (cmbComPort.Items.Count > 0)
                {
                        try
                        {
                            serialPort1.BaudRate = 9600;
                            serialPort1.PortName = cmbComPort.SelectedItem.ToString();
                            serialPort1.Open();
                            btnConnect.Text = "Disconnect";
                            timer1.Enabled = true;
                        }
                        catch (Exception ex)
                        {
                            MessageBox.Show(ex.Message);
                        }
                }
            }
            else
            {
                try
                {
                    serialPort1.Close();
                    btnConnect.Text = "Connect";
                    timer1.Enabled = false;
                }
                catch (Exception ex)
                {
                    MessageBox.Show(ex.Message);
                }
            }
        }

        //Store data in the queue when data is received
        private void serialPort1_DataReceived(object sender, System.IO.Ports.SerialDataReceivedEventArgs e)
        {
            int currentByte;
            
            while (serialPort1.IsOpen && serialPort1.BytesToRead != 0)
            {
                numberOfDataPoints++;
                currentByte = serialPort1.ReadByte();
                dataQueue.Enqueue(currentByte); 
            }
        }

        //If connection was not estbalished (no data received),
        //try connecting again
        private void timer1_Tick(object sender, EventArgs e)
        {
            //Adding a check to reconect to the serial port if there is no data streaming
            if (autoReconnectBox.Checked==true && numberOfDataPoints == 0 && serialPort1.IsOpen == true)
            {
                try
                {
                    serialPort1.Close();
                    serialPort1.BaudRate = 9600;
                    serialPort1.PortName = cmbComPort.SelectedItem.ToString();
                    serialPort1.Open();
                }
                catch (Exception ex)
                {
                    //MessageBox.Show(ex.Message);
                }
                        
            }

            numberOfDataPoints = 0;
        }

        private void cmbComPort_DropDown(object sender, EventArgs e)
        {
            ComPortUpdate();
        }

        //This timer event is used to process the data added to the queue
        //from the serial port
        private void receiveTimer_Tick(object sender, EventArgs e)
        {
            Int32 currentByte;
            while (dataQueue.Count > 0)
            {
                //Dequeue a transmitted byte
                dataQueue.TryDequeue(out currentByte);
                txtRawSerial.AppendText(currentByte.ToString() + ", ");

                //Exporting FRAM Data Set
                if (checkboxSaveFile.Checked == true)
                {
                    switch (myExportState)
                    {
                        case exportState.lightHigh:
                            exportData = currentByte << 8;
                            myExportState = exportState.lightLow;
                            break;
                        case exportState.lightLow:
                            exportData = exportData | currentByte;
                            myExportState = exportState.startIndicator;
                            break;
                        case exportState.startIndicator:
                            startIndicator = currentByte;
                            myExportState = exportState.lightHigh;
                            outputFile.Write(exportData.ToString() + ", " + startIndicator.ToString() + "\n");
                            break;
                    }
                    exportCount++;
                    //If the dataset export is complete, stop exporting by
                    //unchecking the savefile box
                    if (exportCount >= (DATASET_SIZE * 3))
                    {
                        exportCount = 0;
                        checkboxSaveFile.Checked = false;
                    }
                }
                
                //Regular sensor data stream
                //Reads data coming in from the serial port
                else
                {
                    if (currentByte == 255)
                        myState = state.start;

                    switch (myState)
                    {
                        case state.start:
                            lightTextBox.Text = lightData.ToString();
                            SS0TextBox.Text = soilSensorData[0].ToString();
                            SS1TextBox.Text = soilSensorData[1].ToString();
                            SS2TextBox.Text = soilSensorData[2].ToString();
                            SS3TextBox.Text = soilSensorData[3].ToString();
                            myState = state.lightHigh;
                            break;

                        //LIGHT DATA
                        case state.lightHigh:
                            lightData = currentByte << 8;
                            myState = state.lightLow;
                            break;
                        case state.lightLow:
                            lightData = lightData | currentByte;
                            myState = state.SS0High;
                            break;

                        //SS0 DATA
                        case state.SS0High:
                            soilSensorData[0] = currentByte << 8;
                            myState = state.SS0Low;
                            break;
                        case state.SS0Low:
                            soilSensorData[0] = soilSensorData[0] | currentByte;
                            myState = state.SS1High;
                            break;

                        //SS1 DATA
                        case state.SS1High:
                            soilSensorData[1] = currentByte << 8;
                            myState = state.SS1Low;
                            break;
                        case state.SS1Low:
                            soilSensorData[1] = soilSensorData[1] | currentByte;
                            myState = state.SS2High;
                            break;

                        //SS2 DATA
                        case state.SS2High:
                            soilSensorData[2] = currentByte << 8;
                            myState = state.SS2Low;
                            break;
                        case state.SS2Low:
                            soilSensorData[2] = soilSensorData[2] | currentByte;
                            myState = state.SS3High;
                            break;

                        //SS3 DATA
                        case state.SS3High:
                            soilSensorData[3] = currentByte << 8;
                            myState = state.SS3Low;
                            break;
                        case state.SS3Low:
                            soilSensorData[3] = soilSensorData[3] | currentByte;
                            myState = state.escapeHigh;
                            break;

                        //ESCAPE "BYTE"
                        case state.escapeHigh:
                            escape = currentByte << 8;
                            myState = state.escapeLow;
                            break;
                        case state.escapeLow:
                            escape = escape | currentByte;
                            myState = state.start;          //this line is redundant, nice safety tho
                            processEscape();
                            break;
                    }
                }
            }
        }
        //This function proccess the escape byte
        //By updating any received data to 255
        //based on the bits indicated in the escape byte
        public void processEscape()
        {
            //escape[9] = lightData High
            //escape[8] = lightData Low
            //escape[7] = SS0 High
            //escape[6] = SS0 Low
            //escape[5] = SS1 High
            //escape[4] = SS1 Low
            //escape[3] = SS2 High
            //escape[2] = SS2 Low
            //escape[1] = SS3 High
            //escape[0] = SS3 Low

            //LIGHT BYTES
            if ((escape & 0b1000000000) != 0)
                lightData |= 0xFF00;
            if ((escape & 0b0100000000) != 0)
                lightData |= 0x00FF;

            //SS0 BYTES
            if ((escape & 0b0010000000) != 0)
                soilSensorData[0] |= 0xFF00;
            if ((escape & 0b0001000000) != 0)
                soilSensorData[0] |= 0x00FF;

            //SS1 BYTES
            if ((escape & 0b0000100000) != 0)
                soilSensorData[1] |= 0xFF00;
            if ((escape & 0b0000010000) == 1)
                soilSensorData[1] |= 0x00FF;

            //SS2 BYTES
            if ((escape & 0b0000001000) != 0)
                soilSensorData[2] |= 0xFF00;
            if ((escape & 0b0000000100) == 1)
                soilSensorData[2] |= 0x00FF;

            //SS3 BYTES
            if ((escape & 0b0000000010) != 0)
                soilSensorData[3] |= 0xFF00;
            if ((escape & 0b0000000001) != 0)
                soilSensorData[3] |= 0x00FF;
        }

        //Choose a file to save the dataset to
        private void butSelectFile_Click(object sender, EventArgs e)
        {
            //Launches a save file dialog, sets a default directory, and appends .csv to the end of the filename
            SaveFileDialog myDialogBox = new SaveFileDialog();
            myDialogBox.InitialDirectory = @"X:\School\UBC Years\MECH 4\MECH 423\Project\Data";
            myDialogBox.ShowDialog();
            txtFileName.Text = myDialogBox.FileName.ToString() + ".CSV";
        }

        //If the checkbox gets ticked, create the file
        private void checkboxSaveFile_CheckedChanged_1(object sender, EventArgs e)
        {
            //Prepares a file for writing if the savefile checkbox is checked
            if (checkboxSaveFile.Checked == true)
            {
                outputFile = new StreamWriter(txtFileName.Text);
                //Add Column headers to the dataset
                outputFile.Write("Light Sensor Reading, Start Indicator Value \n");
            }
            else
                outputFile.Close();
        }
    }
}
