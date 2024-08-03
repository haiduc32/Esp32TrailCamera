/*
  This is derivative work, building a Trail Camera using Seedstudio XIAO ESP32S3 with Sense shield.

  BASED ON:

  XIAO ESP32S3 Sense Camera with Microphone Demo
  xiao-camera-mic-demo.ino
  Tests onboard Camera, MEMS Microphone, and MicroSD Card
  Takes a picture and a 10-second recording when Touch Switch is pressed
  Saves to MicroSD card in JPG & WAV format
  
  DroneBot Workshop 2023
  https://dronebotworkshop.com
*/

// Include required libraries
#include <Arduino.h>
#include <esp_camera.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <esp_sleep.h>
#include <WiFi.h>

// Define camera model & pinout
#define CAMERA_MODEL_XIAO_ESP32S3  // Has PSRAM
#include "camera_pins.h"

#define CAMERA_PWDN GPIO_NUM_42

// Audio record time setting (in seconds, max value 240)
#define RECORD_TIME 10

// Audio settings
#define SAMPLE_RATE 16000U
#define SAMPLE_BITS 16
#define WAV_HEADER_SIZE 44
#define VOLUME_GAIN 3

// Camera status variable
bool camera_status = false;

// MicroSD status variable
bool sd_status = false;

// File Counter
int fileCount = 1;

// Touch Switch variables
int threshold = 1500;  // Adjust if not responding properly
bool touch1detected = false;

// Save pictures to SD card
void photo_save(const char *fileName) {
  // Take a photo
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Failed to get camera frame buffer");
    return;
  }
  // Save photo to file
  writeFile(SD, fileName, fb->buf, fb->len);

  // Release image buffer
  esp_camera_fb_return(fb);

  Serial.println("Photo saved to file");
}

// SD card write file
void writeFile(fs::FS &fs, const char *path, uint8_t *data, size_t len) {
  Serial.printf("Writing file: %s\r\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  if (file.write(data, len) == len) {
    Serial.println("File written");
  } else {
    Serial.println("Write failed");
  }
  file.close();
}

// Callback function for touch switch
void gotTouch1() {
  touch1detected = true;
}

// Camera Parameters for setup
void CameraParameters() {
  // Define camera parameters
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  // if PSRAM IC present, init with UXGA resolution and higher JPEG quality
  //                      for larger pre-allocated frame buffer.
  if (config.pixel_format == PIXFORMAT_JPEG) {
    if (psramFound()) {
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      // Limit the frame size when PSRAM is not available
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  } else {
    // Best option for face detection/recognition
    config.frame_size = FRAMESIZE_VGA;//FRAMESIZE_240X240;
#if CONFIG_IDF_TARGET_ESP32S3
    config.fb_count = 2;
#endif
  }

  // Camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }
}

int countFiles() {
  File root = SD.open("/");

  if(!root){
    Serial.println("Failed to open root directory");
    return 0;
  }
  int count = 0;
  File file = root.openNextFile();
  while(file){
    if(file.isDirectory()){
      // do nothing? :/
    } else {
      count++;
    }
     file = root.openNextFile();
  }

  return count;
}

unsigned long startMillis;
unsigned long elapsedMillis;

bool status = false;

void setup() {
  //disable hold!!
  gpio_hold_dis(CAMERA_PWDN);

  pinMode(CAMERA_PWDN, OUTPUT);
  digitalWrite(CAMERA_PWDN, LOW);

  pinMode(D3, OUTPUT);
  pinMode(D4, OUTPUT);
  pinMode(D5, OUTPUT);
  digitalWrite(D3, LOW);
  digitalWrite(D4, LOW);
  digitalWrite(D5, LOW);
  digitalWrite(D3, HIGH);
  // Record start time
  // Start Serial Monitor, wait until port is ready
  //delay(1000);
  startMillis = millis(); 
  Serial.begin(115200);
    //sleep(3000);
  
  // // wait up to 3 seconds for serial debugging
  // int sCounter = 30;
  // while (!Serial)
  // {
  //   delay(100);
  //   sCounter--;

  //   if (sCounter == 0) continue;
  // }

  delay(1000);

  // Disable Wi-Fi and Bluetooth
  WiFi.mode(WIFI_OFF);
  btStop();

  // Reduce CPU frequency
  setCpuFrequencyMhz(80); // Lower to 80 MHz from default 240 MHz

  digitalWrite(D4, HIGH);
  
  Serial.println("checkpoint!");

  delay(100);

  // Define Camera Parameters and Initialize
  CameraParameters();
  //digitalWrite(D5, HIGH);

  // Camera is good, set status
  camera_status = true;
  Serial.println("Camera OK!");

  Serial.println("Attempting to mount SD card..");
  while (!SD.begin(21)) {
    Serial.println("Failed to mount MicroSD Card! Will retry.");
  }
  Serial.println("SD card mounted");

  // Determine what type of MicroSD card is mounted
  uint8_t cardType = SD.cardType();

  if (cardType == CARD_NONE) {
    Serial.println("No MicroSD card inserted!");
    return;
  }

  // Print card type
  Serial.print("MicroSD Card Type: ");
  if (cardType == CARD_MMC) {
    Serial.println("MMC");
  } else if (cardType == CARD_SD) {
    Serial.println("SDSC");
  } else if (cardType == CARD_SDHC) {
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }
  // MicroSD is good, set status
  sd_status = true;

  // Configure wakeup time: 1 second = 1,000,000 microseconds
  esp_sleep_enable_timer_wakeup(10000000);

  Serial.println("end setup()");

  elapsedMillis = millis() - startMillis;  // Calculate elapsed time

  Serial.print("Elapsed time in milliseconds for setup(): ");
  Serial.println(elapsedMillis);

  digitalWrite(D5, HIGH);
}

void loop() {
  // Serial.println("loop");
  // Make sure the camera and MicroSD are ready
  if (camera_status && sd_status) {
    // Create image file name
    char imageFileName[32];
    sprintf(imageFileName, "/image%d.jpg", fileCount);

    // Take a picture
    photo_save(imageFileName);
    Serial.printf("Saving picture: %s\r\n", imageFileName);

    // Increment file counter
    fileCount++;
    
    // Reset the touch variable
    touch1detected = false;

    elapsedMillis = millis() - startMillis;  // Calculate elapsed time

    // Serial.print("Elapsed time in milliseconds: ");
    // Serial.println(elapsedMillis);
    status = !status;
    digitalWrite(D4, status);
    
    
    // Enter deep sleep
    digitalWrite(CAMERA_PWDN, HIGH);
    gpio_hold_en(CAMERA_PWDN);
    gpio_deep_sleep_hold_en(); // Enable hold during deep sleep

    Serial.println("Going to sleep now");
    delay(10000);
    
    esp_deep_sleep_start();
  }
}
