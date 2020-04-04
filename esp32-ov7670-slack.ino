#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include "OV7670.h"
#include "BMP.h"
#include <img_converters.h>
#include "myWiFi.h"

/*
 * to_jpg.cpp
 * bug fix
*/
bool fmt2jpg_(uint8_t *src, size_t src_len, uint16_t width, uint16_t height, pixformat_t format, uint8_t quality, uint8_t **out, size_t *out_len);

/*
 * VO7670 camera
*/
const int SIOD = 15;
const int SIOC = 17;
const int VSYNC = 5;
const int HREF = 2;
const int XCLK = 4;
const int PCLK = 18;
const int D0 = 12;
const int D1 = 26;
const int D2 = 14;
const int D3 = 25;
const int D4 = 27;
const int D5 = 33;
const int D6 = 16;
const int D7 = 32;

OV7670 *camera;
unsigned char bmpHeader[BMP::headerSize];

#define PIN_CAMERAPW GPIO_NUM_13

/*
 * slack
*/
#define HTTPS_HOST "slack.com"
#define SLACK_METHOD_PATH "/api/files.upload"
#define HTTPS_PORT 443
#define CAPTURE_RETRY_COUNT 2

/*
 * files.upload api sample
 * https://slack.com/api/files.upload?token=xoxp-xxxxxxxx&channels=XXXXXXXX&filename=capture.jpg&filetype=image%2Fjpeg&initial_comment=%25Y%25m%25d%25H%25M%25S&pretty
 */
const char *slack_api_token = "xoxp-xxxxxxxxxxxx-xxxxxxxxxxxx-xxxxxxxxxxxx-xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";

const char *slack_channel = "your-channel";

const char *request_method = "POST %s HTTP/1.1\r\n"
                             "Host: %s\r\n"
                             "User-Agent: esp32-ov7670\r\n"
                             "Accept: */*\r\n"
                             "Content-Length: %d\r\n"
                             "Content-Type: multipart/form-data; boundary=------------------------ef73a32d43e7f04d\r\n"
                             "Expect: 100-continue\r\n"
                             "Authorization: Bearer %s\r\n\r\n";

const char *request_content = "--------------------------ef73a32d43e7f04d\r\n"
                              "Content-Disposition: form-data; name=\"token\"\r\n\r\n"
                              "%s\r\n"
                              "--------------------------ef73a32d43e7f04d\r\n"
                              "Content-Disposition: form-data; name=\"channels\"\r\n\r\n"
                              "%s\r\n"
                              "--------------------------ef73a32d43e7f04d\r\n"
                              "Content-Disposition: form-data; name=\"filename\"\r\n\r\n"
                              "capture.jpg\r\n"
                              "--------------------------ef73a32d43e7f04d\r\n"
                              "Content-Disposition: form-data; name=\"filetype\"\r\n\r\n"
                              "image/jpeg\r\n"
                              "--------------------------ef73a32d43e7f04d\r\n"
                              "Content-Disposition: form-data; name=\"initial_comment\"\r\n\r\n"
                              "No.%d %s (%d) %.2fV\r\n"
                              "--------------------------ef73a32d43e7f04d\r\n"
                              "Content-Disposition: form-data; name=\"file\"; filename=\"capture.jpg\"\r\n"
                              "Content-Type: image/jpeg\r\n"
                              "Content-Length: %d\r\n\r\n";

const char *request_end = "\r\n--------------------------ef73a32d43e7f04d--\r\n";

WiFiClientSecure client;

RTC_DATA_ATTR int bootCounter = 0;
RTC_DATA_ATTR int errorNo = 0;

#define uS_TO_S_FACTOR 1000000ULL  /* Conversion factor for micro seconds to seconds */
int TimeTable[] = {7, 12, 17};  // wakeup hours
int retry = 0;

/*
 * self voltage
 */
#define PIN_SELFVOLT_IN GPIO_NUM_39
#define PIN_SELFVOLT_OUT GPIO_NUM_23
float selfVolt = 0.0;

void setup()
{
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //disable brownout detector

  Serial.begin(115200);

  ++bootCounter;
  Serial.printf("Boot number: %d ***************************************************\n", bootCounter);

  // camera ON
  pinMode(PIN_CAMERAPW, OUTPUT);
  digitalWrite(PIN_CAMERAPW, HIGH);

  pinMode(GPIO_NUM_12, PULLUP);

  // self voltage
  selfVolt = getSelfVoltage();
  if (selfVolt < 2.5)
  {
    deepsleep(true);
  }

  // initialize camera
  camera = new OV7670(OV7670::Mode::QQVGA_RGB565, SIOD, SIOC, VSYNC, HREF, XCLK, PCLK, D0, D1, D2, D3, D4, D5, D6, D7);
  BMP::construct16BitHeader(bmpHeader, camera->xres, camera->yres);
  Serial.println("camera init done");

  // connect wifi
  if (connectWiFi())
  {
    while(!capture())
    {
      retry++;
      // retry loop
      if(retry > CAPTURE_RETRY_COUNT) {
        //errorNo = 2;
        break;
      }
    }
  }
  else {
    errorNo = 1;
  }

  // disconnect wifi
  disconnectWiFi();

  // deepsleep
  deepsleep(false);
}

void loop()
{
}

bool capture()
{
  char status[64] = {0};
  char timestr[32];
  char content[800];
  char request[360];
  struct tm timeinfo;
  int camera_len = (camera->xres * camera->yres * 2);
  uint8_t *jpg_buf = NULL;
  size_t jpg_buf_len = 0;

  camera->oneFrame();

  if (!fmt2jpg_(camera->frame, camera_len, camera->xres, camera->yres, PIXFORMAT_RGB565, 90, &jpg_buf, &jpg_buf_len))
  {
    Serial.println("fmt2jpg failed");
    errorNo = 3;
    return false;
  }
  //Serial.printf("fmt2jpg : jpg_buf_len:%d\n", jpg_buf_len);

  if (!client.connect(HTTPS_HOST, HTTPS_PORT))
  {
    Serial.println("Connection failed");
    free(jpg_buf);
    errorNo = 4;
    return false;
  }

  getLocalTime(&timeinfo);
  strftime(timestr, sizeof(timestr), "%Y/%m/%d %H:%M:%S", &timeinfo);
  snprintf(content, sizeof(content), request_content, slack_api_token, slack_channel, bootCounter, timestr, errorNo, selfVolt, jpg_buf_len);
  //Serial.printf("content len:%d\n", strlen(content));

  int content_len = jpg_buf_len + strlen(content) + strlen(request_end);
  snprintf(request, sizeof(request), request_method, SLACK_METHOD_PATH, HTTPS_HOST, content_len, slack_api_token);
  //Serial.printf("request len:%d\n", strlen(request));

  client.print(request);
  //Serial.print(request);
  client.readBytesUntil('\r', status, sizeof(status));
  //Serial.println(status);

  if (strcmp(status, "HTTP/1.1 100 Continue") != 0)
  {
    Serial.printf("Unexpected response: %s\n", status);
    client.stop();
    free(jpg_buf);
    errorNo = 5;
    return false;
  }

  client.print(content);
  //Serial.print(content);
  //Serial.printf("width: %d, height: %d, len: %d\n", camera->xres, camera->yres, jpg_buf_len);

  uint8_t *image = jpg_buf;
  size_t size = jpg_buf_len;
  size_t offset = 0;
  size_t ret = 0;
  while (1)
  {
    ret = client.write(image + offset, size);
    offset += ret;
    size -= ret;
    if (jpg_buf_len == offset)
      break;
  }
  free(jpg_buf);

  client.flush();
  client.print(request_end);

  client.readStringUntil('\n');
  delay(2000); // need!! slack.com interval
  client.stop();
  Serial.printf("posted slack: %s\n", timestr);
  errorNo = 0;

  return true;
}

void deepsleep(bool forever)
{
  // camera power off
  digitalWrite(PIN_CAMERAPW, LOW); // camera off

  if (forever)
  {
    Serial.println("Going to sleep now forever");
  }
  else
  {
    struct tm next;
    struct tm now;
    getLocalTime(&now);

    //int TimeTable[] = {7, 12, 17};  // wakeup hours
    int timelen = sizeof(TimeTable)/sizeof(int);
    int nowHour = now.tm_hour + 1;  // ESP32のdeepsleepは早く起きるので
    int i=0, d=0;
    for(i=0; i<timelen; i++) {
      if(nowHour < TimeTable[i]) {
        break;
      }
    }
    if(i == timelen) {
      i = 0; d = 1;
    }
    next = {0, 0, TimeTable[i], now.tm_mday + d, now.tm_mon, now.tm_year};
    // 現時刻から点灯させる時刻の差(st)を求める
    time_t sec = mktime(&next) - mktime(&now);

    esp_sleep_enable_timer_wakeup(sec * uS_TO_S_FACTOR * 1.006);
    Serial.printf("Going to sleep now: %d sec\n", sec);
  }

  esp_deep_sleep_start();
}

/*
 * get self voltage
 * 
 * OUT - 10k - + - 10k - GND
 *             |
 *             + - IN : analogRead
 * 
 *  OUT: PIN_SELFVOLT_OUT
 *  IN : PIN_SELFVOLT_IN
 */
float getSelfVoltage()
{
  pinMode(PIN_SELFVOLT_OUT, OUTPUT);
  digitalWrite(PIN_SELFVOLT_OUT, HIGH);
  delay(10);
  float analog = analogRead(PIN_SELFVOLT_IN);
  float selfVolt = analog / 4096 * 3.9 * 2;
  Serial.printf("selfVolt=%.2f\n", selfVolt);
  digitalWrite(PIN_SELFVOLT_OUT, LOW);

  return selfVolt;
}
