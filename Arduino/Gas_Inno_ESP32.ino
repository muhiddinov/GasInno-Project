#include <SoftwareSerial.h>
#include <TinyGPS++.h>
#include <TimeLib.h>
#include <ArduinoJson.h>

#define GSM_PWR_PIN 27
#define GSM_STA_PIN 21
#define GSM_RST_PIN 5
#define SENSOR_PIN  35
#define RELAY_PIN   23
#define GAS_SF      10

StaticJsonDocument<500> doc;
StaticJsonDocument<500> conf;
SoftwareSerial Serial_GSM (25, 26);
SoftwareSerial Serial_GPS (18, 19);
TinyGPSPlus gps;
tmElements_t te;
time_t unixTime;
uint32_t time_span = 0;

//String configuration = "{
//                       \"url\": \"http://gas-inno.herokuapp.com/api/geo-data?\",\
//                       \"url_sup\": \"http://gas-inno.herokuapp.com/api/geo-data-supply?\",\
//                       \"token\": \"vKhR3NmsfT6S1v6PVuwIC8fnGrMHNHKlkW1tjiMRQWNqjHp0saDTfl5iTHpWoeZ7vj03Zc2ew1LWR8ZX\",\
//                       \"gas_max\": 1150,\
//                       \"gas_min\": 300,\
//                       \"relay\": 1,\
//                       \"device_id\": 21,\
//                       }";

String        url = "http://176.96.243.202:8011/api/geo-data?";
String        url_gas = "http://176.96.243.202:8011/api/geo-data-supply?";
String        url_stopped = "http://176.96.243.202:8011/api/geo-data-stopped?";
uint32_t      device_id = 32;
String        token = "9KsNkjxeWxWZ2hKGxE4nPjUNmBpQ1nJYWVA3uuy7elUmPBos9i7Oa1wwI9tWLae0UTjYID2IHYv3zxaI";
bool          relay_state = 0;
int           fuel_gas = 200;
String        _lat = "41.311081";
String        _long = "69.240562";
int           _speed = 70;
bool          restored = 0;


uint8_t getStat = 0;
uint8_t httpResp = 0;
uint8_t fixcmd = 0;
bool fix_cmd = false, supply_gas = false;
uint8_t old_fuel_gas = 0;
uint32_t supply_time_1 = 0, supply_time_2 = 0;
bool supply_time = 0;
uint8_t gas_level = 0;
uint32_t gsm_power_time = 0;


double latitude = 0.0, longitude = 0.0, old_location = 0.0;
uint32_t curtime_gps = 0, curtime_gsm = 0, curtime_http = 0, send_pocket_size = 0;
uint8_t old_cmd_gsm = 0;
uint8_t csq = 0;
uint8_t cbc = 0;
float voltage = 0.0;
bool next_cmd = true, waitHttpAction = false, send_geo_data = false;
bool internet = false, httpinit = false, checked_internet = false, gsm_power_on = false;

#define AT_CHK      0
#define AT_CSQ      1
#define AT_CBC      2
#define AT_NET_CON  3
#define AT_NET_APN  4
#define AT_NET_ON   5
#define AT_NET_OFF  6
#define AT_NET_CHK  7
#define AT_HTTPINIT 8
#define AT_HTTPCID  9
#define AT_HTTPURL  10
#define AT_HTTPCON  11
#define AT_HTTPACG  12
#define AT_HTTPACP  13
#define AT_HTTPRD   14
#define AT_HTTPTM   15

String commands[] = {
  "AT",                                             // 1  Check communication
  "AT+CSQ",                                         // 2  Check GSM ant level
  "AT+CBC",                                         // 3  Check battery level
  "AT+SAPBR=3,1,\"CONTYPE\", \"GPRS\"",             // 4  Set connection type for gprs
  "AT+SAPBR=3,1,\"APN\",\"DEFAULT\"",               // 5  Set APN for any GSM communication
  "AT+SAPBR=1,1",                                   // 6  Turn on internet
  "AT+SAPBR=0,1",                                   // 7  Turn off internet
  "AT+SAPBR=2,1",                                   // 8  Check internet connection and IP
  "AT+HTTPINIT",                                    // 9  Http initializing
  "AT+HTTPPARA=\"CID\",1",                          // 10 Check
  "AT+HTTPPARA=\"URL\",",                           // 11 Set http url
  "AT+HTTPPARA=\"CONTENT\",\"APPLICATION/JSON\"",   // 12 Content type as application/json
  "AT+HTTPACTION=0",                                // 13 Http get request
  "AT+HTTPACTION=1",                                // 14 Http post request
  "AT+HTTPREAD",                                    // 15 Read http response data
  "AT+HTTPTERM",                                    // 16 Http closing
};

String make_api() {
  return ("device_token=" + token + "&lat=" + _lat + "&long=" + _long + "&device_id=" + String(device_id)
          + "&relay_state=" + String(relay_state) + "&fual_gas=" + String(fuel_gas) + "&speed=" + String(_speed)
          + "&datetime=" + String(unixTime) + "&restored=" + String(restored));
}


String make_api_stopped () {
  return ("device_token=" + token + "&device_id=" + String(device_id) + "&relay_state=" + String(relay_state));
}

struct cmdQueue {
  String cmd [16];
  uint8_t cmd_id[16];
  int k;
  void init () {
    k = 0;
    for (int i = 0; i < 16; i++) {
      cmd_id[i] = -1;
    }
  }
  void addQueue (String msg, uint8_t msg_id) {
    //Serial.println(msg);
    if (msg_id == AT_HTTPURL && msg.length() < 10) {
      ESP.restart();
    }
    cmd[k] = msg;
    cmd_id[k++] = msg_id;
    if (k > 15) k = 0;
  }
  void sendCmdQueue () {
    if (k > 0) {
      if (!waitHttpAction) {
        Serial_GSM.println(cmd[0]);
        //Serial.println(cmd[0]);
        old_cmd_gsm = cmd_id[0];
        if (cmd_id[0] == AT_HTTPACG || cmd_id[0] == AT_HTTPACP) {
          waitHttpAction = true;
        }
        k --;
        next_cmd = false;
        for (int i = 0; i < k; i++) {
          cmd[i] = cmd[i + 1];
          cmd[i + 1] = "";
          cmd_id[i] = cmd_id[i + 1];
          cmd_id[i + 1] = -1;
        }
      }
    }
  }
};

cmdQueue queue;

void setup() {
  Serial.begin(115200);
  Serial_GSM.begin(9600);
  Serial_GPS.begin(9600);
  pinMode(GSM_PWR_PIN, OUTPUT);
  digitalWrite(GSM_PWR_PIN, HIGH);
  delay(3000);
  digitalWrite(GSM_PWR_PIN, LOW);
  Serial_GSM.println("AT");
  while (!gsm_power_on) {
    if (Serial_GSM.available()) {
      String ss = Serial_GSM.readStringUntil('\n');
      Serial.println(">>> " + ss);
      if (ss.indexOf("OK") >= 0) {
        gsm_power_on = true;
      }
    }
    if (millis() - gsm_power_time > 3000) {
      digitalWrite(GSM_PWR_PIN, HIGH);
      delay(3000);
      digitalWrite(GSM_PWR_PIN, LOW);
      delay(1000);
      Serial_GSM.println("AT");
      delay(1000);
      gsm_power_time = millis();
    }
  }
  pinMode(RELAY_PIN, OUTPUT);
  queue.init();
  queue.addQueue(commands[AT_CHK], AT_CHK);
  queue.addQueue(commands[AT_CBC], AT_CBC);
  queue.addQueue(commands[AT_CSQ], AT_CSQ);
  queue.addQueue(commands[AT_NET_CON], AT_NET_CON);
  queue.addQueue(commands[AT_NET_APN], AT_NET_APN);
  queue.addQueue(commands[AT_NET_ON], AT_NET_ON);
  queue.addQueue(commands[AT_NET_CHK], AT_NET_CHK);
}

void loop() {
  //  if (Serial_GSM.available()) {
  //    Serial.println(Serial_GSM.readString());
  //  }
  //  if (Serial.available()) {
  //    Serial_GSM.println(Serial.readString());
  //  }
  if (Serial_GPS.available()) gps.encode(Serial_GPS.read());
  if (millis() - time_span > 1000) {
    time_span = millis();
    displayInfo();
    getStat ++;
    httpResp ++;
    fixcmd ++;
    if (fixcmd > 50) fixcmd = 0;
    fuel_gas = map(analogRead(SENSOR_PIN), 578, 3450, 0, 300);
    if (fuel_gas > 250) fuel_gas = 250; 
    if (fuel_gas < 0) fuel_gas = 0;
    fuel_gas = 10*((fuel_gas + 5)/10);
    if (fuel_gas - old_fuel_gas > 50) {
      if (!supply_gas && httpinit) {
        httpinit = false;
        supply_gas = true;
        supply_time = false;
        queue.addQueue(commands[AT_HTTPCID], AT_HTTPCID);
        queue.addQueue(commands[AT_HTTPURL] + "\"" + url_gas + make_api() + "\"", AT_HTTPURL);
        queue.addQueue(commands[AT_HTTPACG], AT_HTTPACG);
        queue.addQueue(commands[AT_HTTPRD], AT_HTTPRD);
        queue.addQueue(commands[AT_HTTPTM], AT_HTTPTM);
      }
    }
    if (fuel_gas - old_fuel_gas < 0) {
      if (!supply_time) {
        gas_level = fuel_gas;
        supply_time_1 = millis()/1000;
        supply_time = true;
      }
      else {
        supply_time_2 = millis()/1000;
      }
    }
    if (supply_time_2 - supply_time_1 > 1800) {
      supply_time_1 = millis()/1000;
      if (gas_level - fuel_gas > 5) {
        supply_gas = false;
      }
    }
    old_fuel_gas - fuel_gas;
  }
  if (!next_cmd) {
    if (!fix_cmd) {
      fixcmd = 0;
      fix_cmd = true;
    }
    if (fixcmd > 30) {
      waitHttpAction = false;
      next_cmd = true;
      fix_cmd = false;
    }
  }
  if (next_cmd && millis() - curtime_http > 300) {
    curtime_http = millis();
    queue.sendCmdQueue();
  }
  if (!internet && checked_internet) {
    queue.addQueue(commands[AT_NET_ON], AT_NET_ON);
    checked_internet = false;
  }
  if (getStat > 5 && !waitHttpAction) {
    getStat = 0;
    queue.addQueue(commands[AT_CBC], AT_CBC);
    queue.addQueue(commands[AT_CSQ], AT_CSQ);
    queue.addQueue(commands[AT_NET_CHK], AT_NET_CHK);
  }
  if (httpResp > 5 && internet && !waitHttpAction) {
    httpResp = 0;
    queue.addQueue(commands[AT_HTTPTM], AT_HTTPTM);
    queue.addQueue(commands[AT_HTTPINIT], AT_HTTPINIT);
  }
  if (httpinit && send_geo_data) {
    httpinit = false;
    if (_speed > 0) {
      queue.addQueue(commands[AT_HTTPCID], AT_HTTPCID);
      queue.addQueue(commands[AT_HTTPURL] + "\"" + url + make_api() + "\"", AT_HTTPURL);
      queue.addQueue(commands[AT_HTTPACG], AT_HTTPACG);
      queue.addQueue(commands[AT_HTTPRD], AT_HTTPRD);
      queue.addQueue(commands[AT_HTTPTM], AT_HTTPTM);
    } else {
      queue.addQueue(commands[AT_HTTPCID], AT_HTTPCID);
      queue.addQueue(commands[AT_HTTPURL] + "\"" + url_stopped + make_api_stopped() + "\"", AT_HTTPURL);
      queue.addQueue(commands[AT_HTTPACG], AT_HTTPACG);
      queue.addQueue(commands[AT_HTTPRD], AT_HTTPRD);
      queue.addQueue(commands[AT_HTTPTM], AT_HTTPTM);
    }
  }
  checkCommandGSM ();
}

String gsm_data = "";
void checkCommandGSM () {
  if (Serial_GSM.available()) {
    char a = Serial_GSM.read();
    if (a != '\n') {
      gsm_data += a;
    } else {
      if (gsm_data.length() > 1) {
        if (gsm_data.indexOf("{\"data\":") == 0) {
          DeserializationError error = deserializeJson(doc, gsm_data);
          String relay = doc["relay"];
          if (relay == "TURNOFF") {
            digitalWrite(RELAY_PIN, 0);
            relay_state = 0;
          }
          else if (relay == "TURNON") {
            digitalWrite(RELAY_PIN, 1);
            relay_state = 1;
          }
        }
        check_CMD(gsm_data);
      }
      gsm_data = "";
    }
  }
}

void check_CMD (String str) {
  if (old_cmd_gsm == AT_CSQ) {
    if (str.indexOf("+CSQ") == 0) {
      String val = str.substring(6, 8);
      csq = val.toInt();
      next_cmd = true;
      return;
    }
  }
  else if (old_cmd_gsm == AT_CBC) {
    if (str.indexOf("+CBC") == 0) {
      String lvl = str.substring(8, 10);
      String volt = str.substring(11, 15);
      cbc = lvl.toInt();
      voltage = float(volt.toInt() / 10);
      next_cmd = true;
      return;
    }
  }
  else if (old_cmd_gsm == AT_CHK || old_cmd_gsm == AT_HTTPCID || old_cmd_gsm == AT_HTTPCON || old_cmd_gsm == AT_HTTPURL || AT_NET_CON == old_cmd_gsm || AT_NET_APN == old_cmd_gsm) {
    if (str.indexOf("OK") == 0) {
      next_cmd = true;
      return;
    }
  }
  else if (old_cmd_gsm == AT_NET_CHK) {
    if (str.indexOf("+SAPBR: 1,1") == 0) {
      internet = true;
      checked_internet = true;
      next_cmd = true;
      return;
    }
    else if (str.indexOf("+SAPBR: 1,3") == 0) {
      internet = false;
      checked_internet = true;
      next_cmd = true;
      return;
    }
  }
  else if (old_cmd_gsm == AT_NET_ON) {
    if (!str.indexOf("OK") || !str.indexOf("ERROR")) {
      next_cmd = true;
      return;
    }
  }
  else if (old_cmd_gsm == AT_NET_OFF) {
    if (!str.indexOf("OK") || !str.indexOf("ERROR")) {
      next_cmd = true;
      return;
    }
  }
  else if (old_cmd_gsm == AT_HTTPINIT) {
    if (!str.indexOf("OK")) {
      httpinit = true;
      next_cmd = true;
      return;
    } else if (!str.indexOf("ERROR")) {
      httpinit = false;
      next_cmd = true;
      return;
    }
  }
  else if (old_cmd_gsm == AT_HTTPTM) {
    if (str.indexOf("OK") == 0) {
      next_cmd = true;
      return;
    } else if (!str.indexOf("ERROR")) {
      next_cmd = true;
      return;
    }
  }
  else if (old_cmd_gsm == AT_HTTPACG || old_cmd_gsm == AT_HTTPACP) {
    if (str.indexOf("+HTTPACTION:") == 0) {
      send_pocket_size ++;
      waitHttpAction = false;
      next_cmd = true;
      return;
    }
  }
  else if (old_cmd_gsm == AT_HTTPRD) {
    if (str.indexOf("+HTTPREAD:") == 0) {
      next_cmd = true;
      return;
    }
  } else {
    if (str.indexOf("OK") == 0 || str.indexOf("ERROR") == 0) {
      next_cmd = true;
      return;
    }
  }
}

void displayInfo() {
  Serial.println(F("Location: "));
  if (gps.speed.isValid()) {
    _speed = gps.speed.kmph();
    Serial.print("Speed: "); Serial.println(_speed);
  }
  if (gps.location.isValid())
  {
    _lat = String(gps.location.lat(), 6);
    _long = String(gps.location.lng(), 6);
    latitude = gps.location.lat();
    longitude = gps.location.lng();
    send_geo_data = true;
    Serial.print(_lat);
    Serial.print(F(", "));
    Serial.print(_long);
  }
  else
  {
    send_geo_data = false;
    Serial.print(F("INVALID"));
  }
  Serial.println('\n');
  te.Second = gps.time.second();
  te.Hour = (gps.time.hour() + 5) % 24;
  te.Minute = gps.time.minute();
  te.Day = gps.date.day();
  te.Month = gps.date.month();
  te.Year = gps.date.year() - 1970;
  unixTime =  makeTime(te);
  Serial.print(F("Date/Time: "));
  Serial.println(unixTime);
  if (gps.date.isValid())
  {
    Serial.print(gps.date.month());
    Serial.print(F("/"));
    Serial.print(gps.date.day());
    Serial.print(F("/"));
    Serial.print(gps.date.year());
  }
  else
  {
    Serial.print(F("INVALID"));
  }
  Serial.print(F(" "));
  if (gps.time.isValid())
  {
    if ((gps.time.hour() + 5) % 24 < 10) Serial.print(F("0"));
    Serial.print((gps.time.hour() + 5) % 24);
    Serial.print(F(":"));
    if (gps.time.minute() < 10) Serial.print(F("0"));
    Serial.print(gps.time.minute());
    Serial.print(F(":"));
    if (gps.time.second() < 10) Serial.print(F("0"));
    Serial.println(gps.time.second());
  }
  else
  {
    Serial.println(F("INVALID"));
  }
  Serial.print("FuelGas: "); Serial.println(fuel_gas);
  Serial.print("Send Geo Data: "); Serial.println(send_geo_data);
  Serial.print("CSQ:"); Serial.print(csq); Serial.print("  CBC:"); Serial.print(cbc); Serial.print("  E:"); Serial.println(internet);
  Serial.print("Sended packet: "); Serial.println(send_pocket_size);
}
