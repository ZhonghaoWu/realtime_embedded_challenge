#include "mbed.h"
#include <DTW.hpp>
#include <iostream>
#include <vector>
#include "LCD_DISCO_F429ZI.h"
#include "TS_DISCO_F429ZI.h"

SPI spi(PF_9, PF_8, PF_7,PC_1,use_gpio_ssel); // mosi, miso, sclk, cs
InterruptIn int2(PA_2,PullDown);

// touch screeen setup
LCD_DISCO_F429ZI lcd;
TS_DISCO_F429ZI ts;

#define OUT_X_L 0x28
//register fields(bits): data_rate(2),Bandwidth(2),Power_down(1),Zen(1),Yen(1),Xen(1)
#define CTRL_REG1 0x20
//configuration: 200Hz ODR,50Hz cutoff, Power on, Z on, Y on, X on
#define CTRL_REG1_CONFIG 0b01'10'1'1'1'1
//register fields(bits): reserved(1), endian-ness(1),Full scale sel(2), reserved(1),self-test(2), SPI mode(1)
#define CTRL_REG4 0x23
//configuration: reserved,little endian,500 dps,reserved,disabled,4-wire mode
#define CTRL_REG4_CONFIG 0b0'0'01'0'00'0
//register fields(bits): I1_Int1 (1), I1_Boot(1), H_Lactive(1), PP_OD(1), I2_DRDY(1), I2_WTM(1), I2_ORun(1), I2_Empty(1)
#define CTRL_REG3 0x22
//configuration: Int1 disabled, Boot status disabled, active high interrupts, push-pull, enable Int2 data ready, disable fifo interrupts                 
#define CTRL_REG3_CONFIG 0b0'0'0'0'1'000

#define SPI_FLAG 1
#define DATA_READY_FLAG 2

// we will read 60 sigmoid-processed samples (3 seconds of movement at 20Hz sampling rate) for password and user inputs
#define buffer_size 60

// For the user push button, the first push will be for setting password
// Later pushes after the first one wil be for entering password
// To reset password, push the black reset button to reset the device
DigitalIn button(PA_0); // usr push button
DigitalOut green_LED(LED3); // green LED
DigitalOut red_LED(LED4); // red LED

uint8_t write_buf[32];
uint8_t read_buf[32];

EventFlags flags;

// ================================== GUI ================================== 
// GUI Object
struct Card{
  uint16_t x;
  uint16_t y;
  uint16_t w;
  uint16_t h;
  bool state;
};

// For GUI, convert RGB to uint32_t
uint32_t rgb(uint8_t r, uint8_t g, uint8_t b){
  return ((uint32_t)255 << 24 | (uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

// Colors
uint32_t UL_BG = rgb(104, 134, 240);
uint32_t UL_TX = rgb(235, 236, 249);
uint32_t RC_BG = rgb(242, 152, 63);
uint32_t RC_TX = rgb(43, 0, 1);
uint32_t CS_BG_OP = rgb(90, 200, 130);
uint32_t CS_TX_OP = rgb(10, 33, 10);
uint32_t CS_BG_CL = rgb(147, 63, 169);
uint32_t CS_TX_CL = rgb(238, 224, 241);
uint32_t RI_BG = rgb(231, 131, 124);
uint32_t RI_TX = rgb(238, 224, 241);
uint32_t WP_BG = rgb(217, 64, 44);
uint32_t WP_TX = rgb(43, 0, 1);

// GUI Drawing Functions
void drawRoundedRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t r, uint32_t color){
  // Draw the corners
  lcd.SetTextColor(color);
  lcd.FillCircle(x + r, y + r, r);
  lcd.FillCircle(x + w - r - 1, y + r, r);
  lcd.FillCircle(x + r, y + h - r - 1, r);
  lcd.FillCircle(x + w - r - 1, y + h - r - 1, r);

  // Fill the center
  lcd.FillRect(x + r, y, w - 2 * r, h);
  lcd.FillRect(x, y + r, w, h - 2 * r);
}

void drawCard(Card *button, uint32_t bg, uint32_t tc, char str[]){
  drawRoundedRect(button->x, button->y, button->w, button->h, 15, bg);
  lcd.SetTextColor(tc);
  lcd.SetBackColor(bg);
  lcd.SetFont(&Font16);
  lcd.DisplayStringAt(button->x + button->w / 2 - 115, button->y + button->h / 2 - 5, (uint8_t *)str, CENTER_MODE);
}


// ================================== SPI ================================== 
//The spi.transfer function requires that the callback
//provided to it takes an int parameter
void spi_cb(int event){
  flags.set(SPI_FLAG);
};
void data_cb(){
  flags.set(DATA_READY_FLAG);
};

// ================================ Our Functions ==================================
// helper functions for debugging
// used to print arrays
void print_int_arr(int arr[], int len){
  for(int i = 0; i < len; i++) {
    printf("%d ", arr[i]);
  }
}

void print_float_arr(float arr[], int len){
  for(int i = 0; i < len; i++) {
    printf("%f ", arr[i]);
  }
}

void print_vector(vector<vector<double>> v){
  int leny = v.size();
  int lenx = v[0].size();
  for(int i = 0; i < leny; i ++){
      for(int j = 0; j < lenx; j ++){
        printf("%f ", v[i][j]);
      }
      printf("\n");
    }
}

// Transpose a 2d vector of doubles
vector<vector<double>> transpose(vector<vector<double>> &matrix){
  int lenx = matrix.size();
  int leny = matrix[0].size();
  vector<vector<double>> result(leny, vector<double>());
  if(matrix.empty()){
    return result;
  }
  else{
    for(int i = 0; i < lenx; i ++){
      for(int j = 0; j < leny; j ++){
        result[j].push_back(matrix[i][j]);
      }
    }
  }
  return result;
}

// Data Preparation for DTW algorithm
// Save the relavent readings that represent the period of movements from the full-length data array into vectors of doubles

// This movement-extraction is necessary because the full array can contain brief preiods before / after the movement
// While the Dynamic Time Wraping (DTW) altorithm need only the actual movement part to calculate the degree of similarity between the user entry and password
// therefore we need to remove the irrelevant readings of non-movement

// After movement extraction, concatenate a row of index {0, 1, 2, 3, 4, ... } to the vector of data (done in the extract_sample() function)
// The vector would then become a 2d vector of shape (2, n), where 2 represents the data-index pair, and n is the number of samples
// Then to prepare the vector as an input for the DTW algorithm, we need to transpose it into shape (n, 2)
void extract_sample(float data_arr[], vector<vector<double>>& result_vect){
  bool start_flag = true;
  bool end_flag = true;

  int start_index = 0;
  int end_index = buffer_size - 1;

  for(int i = 0; i < buffer_size; i ++){
    float val = data_arr[i];
    if((val <= -0.4 || val >= 0.4) && start_flag){
      start_index = i;
      start_flag = false;
      break;
    }
  }
  for(int i = buffer_size - 1; i > 0; i --){
    float val = data_arr[i];
    if((val >= -0.4 && val <= 0.4) && end_flag){
      end_index = i;
      end_flag = false;
      break;
    }
  }

// save the extracted data into a vector
  vector<double> dummy_vect;
  for(int i = start_index; i < end_index; i ++){
    dummy_vect.push_back(data_arr[i]);
  }

// concatenate the vector of data with a row of indexes
  vector<double> index_vect;
  for(int i = 0; i < dummy_vect.size(); i++){
    index_vect.push_back(i);
  }

  result_vect = {dummy_vect, index_vect};
}


int main() {
  // ================================ SPI and Interrupt Setup ==================================
  // Setup the spi for 8 bit data, high steady state clock,
  // second edge capture, with a 1MHz clock rate
  spi.format(8,3);
  spi.frequency(1'000'000);

  write_buf[0]=CTRL_REG1;
  write_buf[1]=CTRL_REG1_CONFIG;
  spi.transfer(write_buf,2,read_buf,2,spi_cb,SPI_EVENT_COMPLETE );
  flags.wait_all(SPI_FLAG);

  write_buf[0]=CTRL_REG4;
  write_buf[1]=CTRL_REG4_CONFIG;
  spi.transfer(write_buf,2,read_buf,2,spi_cb,SPI_EVENT_COMPLETE );
  flags.wait_all(SPI_FLAG);

  //configure the interrupt to call our function
  //when the pin becomes high
  int2.rise(&data_cb);

  write_buf[0]=CTRL_REG3;
  write_buf[1]=CTRL_REG3_CONFIG;
  spi.transfer(write_buf,2,read_buf,2,spi_cb,SPI_EVENT_COMPLETE );
  flags.wait_all(SPI_FLAG);

  // The gyroscope sensor keeps its configuration between power cycles.
  // This means that the gyroscope will already have it's data-ready interrupt
  // configured when we turn the board on the second time. This can lead to
  // the pin level rising before we have configured our interrupt handler.
  // To account for this, we manually check the signal and set the flag
  // for the first sample.
  if(!(flags.get()&DATA_READY_FLAG)&&(int2.read()==1)){
    flags.set(DATA_READY_FLAG);
  }

  // ================================ Prepare for main() ==================================
  // password data arrays
  float password_x_arr[buffer_size];
  float password_y_arr[buffer_size];
  float password_z_arr[buffer_size];
  // user entry data arrays
  float x_arr[buffer_size];
  float y_arr[buffer_size];
  float z_arr[buffer_size];

  // 2d vectors for storing movement extracted from password
  vector<vector<double>> password_x_vect;
  vector<vector<double>> password_y_vect;
  vector<vector<double>> password_z_vect;

  // indicate whether or not this is the first push of the user button
  bool first_push = true;
  // indicate whether password setup is ongoing
  bool password_setup = false;
  // indicate whether user entry is ongoing
  bool user_entry = false;
  // GUI flags
  bool allowUnlock = false;

  // counter for password setup; will be counted to buffer_size to save the correct amount of samples as password
  int password_setup_count = 0;
  // counter for user entry; will be counted to buffer_size to save the correct amount of samples as user_entry
  int user_entry_count = 0;

  // Components
  Card recordButton = {70, 40, 100, 50, false};
  Card unlockButton = {70, 120, 100, 50, false};
  Card currentStatus = {15, 200, 210, 36, false};
  Card recordingCard = {30, 30, 180, 260, false};
  Card wrongPwCard = {15, 200, 210, 36, false};

  // Draw UI when start up
  lcd.Clear(LCD_COLOR_WHITE);
  drawCard(&recordButton, UL_BG, UL_TX, "RECORD");
  drawCard(&currentStatus, CS_BG_OP, CS_TX_OP, "NO PASSWORD SAVED");
  // Touch Screen Listener
  TS_StateTypeDef TS_State = {0};

  // ======================================== Game Loop ========================================
  while (1) {
    int16_t raw_gx,raw_gy,raw_gz;
    float gx, gy, gz;

    // wait until new sample is ready
    flags.wait_all(DATA_READY_FLAG);
    // prepare the write buffer to trigger a sequential read
    write_buf[0]=OUT_X_L|0x80|0x40;

    // start sequential sample reading
    spi.transfer(write_buf,7,read_buf,8,spi_cb,SPI_EVENT_COMPLETE );
    flags.wait_all(SPI_FLAG);

    // read_buf after transfer: garbage byte, gx_low,gx_high,gy_low,gy_high,gz_low,gz_high
    // Put the high and low bytes in the correct order lowB,Highb -> HighB,LowB
    raw_gx=( ( (uint16_t)read_buf[2] ) <<8 ) | ( (uint16_t)read_buf[1] );
    raw_gy=( ( (uint16_t)read_buf[4] ) <<8 ) | ( (uint16_t)read_buf[3] );
    raw_gz=( ( (uint16_t)read_buf[6] ) <<8 ) | ( (uint16_t)read_buf[5] );

    gx=((float)raw_gx)*(17.5f*0.017453292519943295769236907684886f / 1000.0f);
    gy=((float)raw_gy)*(17.5f*0.017453292519943295769236907684886f / 1000.0f);
    gz=((float)raw_gz)*(17.5f*0.017453292519943295769236907684886f / 1000.0f);

    // ======================================== Data Processing 1: Sigmoid Function ========================================
    // apply sigmoid function on readings
    gx = 1 / (1 + exp(-gx));
    gy = 1 / (1 + exp(-gy));
    gz = 1 / (1 + exp(-gz));

    // ======================================== GUI ======================================== 
    // Get touch screen info
    ts.GetState(&TS_State);
    // If screen is touched
    if (TS_State.TouchDetected){
      // Get position of touch
      uint16_t x = TS_State.X;
      uint16_t y = TS_State.Y;

      // If RECORD button pressed
      if (x >= recordButton.x && x <= (recordButton.x + recordButton.w) &&
          y >= (320 - recordButton.y - recordButton.h) && y <= (320 - recordButton.y) && first_push){
        // Show recording screen and set password_setup mode
        password_setup = true;
        lcd.Clear(LCD_COLOR_WHITE);
        drawCard(&recordingCard, RI_BG, RI_TX, "Is Recording...");
      }
      // If UNLOCK button pressed
      else if (x >= unlockButton.x && x <= (unlockButton.x + unlockButton.w) &&
               y >= (320 - unlockButton.y - unlockButton.h) && y <= (320 - unlockButton.y) && allowUnlock){
        // Show recording screen and set user_entry mode
        user_entry = true;
        lcd.Clear(LCD_COLOR_WHITE);
        drawCard(&recordingCard, RI_BG, RI_TX, "Is Recording...");
      }
    }

    // ======================================== Password Setup State ======================================== 
    // press button for password setup
    // red led will blink during setup
    if(button && first_push){
      password_setup = true;
      lcd.Clear(LCD_COLOR_WHITE);
      drawCard(&recordingCard, RI_BG, RI_TX, "Is Recording...");
    }
    if(password_setup){
      // save the gyroscope reading sample into password data array
      // each axis has its own one-dimensional array
      if(password_setup_count < buffer_size){
        password_x_arr[password_setup_count] = gx;
        password_y_arr[password_setup_count] = gy;
        password_z_arr[password_setup_count] = gz;

        // fliker the red led while collecting the password
        red_LED = 1;
        thread_sleep_for(3000/(buffer_size * 2));
        red_LED = 0;
        thread_sleep_for(3000/(buffer_size * 2));

        password_setup_count += 1;
      }
      // extract and save the movement readings into 2d vectors
      else{
        password_setup = false;
        first_push = false; 

        extract_sample(password_x_arr, password_x_vect);
        extract_sample(password_y_arr, password_y_vect);
        extract_sample(password_z_arr, password_z_vect);

        password_x_vect = transpose(password_x_vect);
        password_y_vect = transpose(password_y_vect);
        password_z_vect = transpose(password_z_vect);

        // When setting finished, set screen to LOCKED
        // Enable UNLOCK button
        allowUnlock = true;
        lcd.Clear(LCD_COLOR_WHITE);
        drawCard(&unlockButton, RC_BG, RC_TX, "UNLOCK");
        drawCard(&currentStatus, CS_BG_CL, CS_TX_CL, "LOCKED");
      }
    }
      
    // ======================================== User Entry State ======================================== 
    // read buffer_size amount of samples
    // prepare data for DTW altorithm
    // compare DTW result with a pre-set threshold to determine answer-correctness

    // LED indicator will tell whether these readings match the correct "password"
    // if user wish to re-enter a "password", press the user button again
    if(button && !first_push){
      user_entry = true;
      lcd.Clear(LCD_COLOR_WHITE);
      drawCard(&recordingCard, RI_BG, RI_TX, "Is Recording...");
    }

    if(user_entry){
      // save sigmoid-processed readings to data array
      if(user_entry_count < buffer_size){
        x_arr[user_entry_count] = gx;
        y_arr[user_entry_count] = gy;
        z_arr[user_entry_count] = gz;

        green_LED = 1;
        thread_sleep_for(3000/(buffer_size * 2));
        green_LED = 0;
        thread_sleep_for(3000/(buffer_size * 2));

        user_entry_count += 1;
      }
      else{
        user_entry = false;
        user_entry_count = 0;

        // Data Preparation for DTW: extract movement, concatenate the row of indexes, and transpose
        vector<vector<double>> x_vect;
        vector<vector<double>> y_vect;
        vector<vector<double>> z_vect;

        extract_sample(x_arr, x_vect);
        extract_sample(y_arr, y_vect);
        extract_sample(z_arr, z_vect);

        x_vect = transpose(x_vect);
        y_vect = transpose(y_vect);
        z_vect = transpose(z_vect);

        // ======================================== Data Processing 2: DTW ========================================
        // Dynamic Time Wrapping algorithm applied here
        // The function returns the smallest-possible "distance" between the user entry and password
        // This distance can be perceived as a score for similarity
        // The smaller the distance, the more similar are the user entry and password
        double x_distance = DTW::dtw_distance_only(x_vect, password_x_vect, 2);
        double y_distance = DTW::dtw_distance_only(y_vect, password_y_vect, 2);
        double z_distance = DTW::dtw_distance_only(z_vect, password_z_vect, 2);
        printf("distances: %f, %f, %f", x_distance, y_distance, z_distance);

        // set distance threshold at 60; this value is retrieved through trials with different movements
        // i.e. distance > 60 indicates a wrong movement, <= 60 indicates a correct movement
        // if answer is considered correct, blink green led 3 times
        if((x_distance <= 100) && (y_distance <= 100) && (z_distance <= 100)){
          for(int i = 0; i < 3; i ++){
            green_LED = 1;
            thread_sleep_for(500);
            green_LED = 0;
            thread_sleep_for(100);
            printf("CORRECT~~~~~~~~~~~~~~~");

            // draw "unlocked" GUI 
            lcd.Clear(LCD_COLOR_WHITE);
            drawCard(&unlockButton, RC_BG, RC_TX, "UNLOCK");
            drawCard(&currentStatus, CS_BG_OP, CS_TX_OP, "UNLOCKED");
          }
        }
        // if the answer is below 90% correct, blink red led 3 times
        else{
          for(int i = 0; i < 3; i ++){
            red_LED = 1;
            thread_sleep_for(500);
            red_LED = 0;
            thread_sleep_for(100);
            printf("WRONG!!!!!!!!!!!!!!!");

            // draw "wrong password" GUI 
            lcd.Clear(LCD_COLOR_WHITE);
            drawCard(&unlockButton, RC_BG, RC_TX, "UNLOCK");
            drawCard(&wrongPwCard, WP_BG, RI_TX, "WRONG PASSWORD");
          }
        }
        printf("=====================\n");

      }
    } // if user_entry

  } // while(1)
} // main()


