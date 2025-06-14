#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>  // Universal Telegram Bot Library written by Brian Lough: https://github.com/witnessmenow/Universal-Arduino-Telegram-Bot
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NTPClient.h>
#include "WiFiUDP.h"
#include "WakeOnLan.h"
#include <ESPping.h>
#include "secrets.h"
#include <ESPmDNS.h>                            // part of the ESP32 framework V1.0
#include <AsyncUDP.h>                           // https://github.com/me-no-dev/ESPAsyncUDP

//-----------------------------------------------------------

const int botRequestDelay = 600;
unsigned long lastTimeBotRan;

const int tiempoPrendido = 30000;
unsigned long ultimoPrendido;

String mensaje[2];
bool hay_mensajes = false;

WiFiClientSecure clientTCP;
WiFiUDP UDP;
WakeOnLan WOL(UDP);
UniversalTelegramBot bot(BOTtoken, clientTCP);
AsyncUDP udps;
TaskHandle_t Task1;

//------------------------APIs--------------------------------

#define API_Joke_Dev "https://sv443.net/jokeapi/v2/joke/Programming"
#define API_cat_fact "https://cat-fact.herokuapp.com/facts/random"
#define API_zen_quotes "https://zenquotes.io/api/random"
#define API_advice "https://api.adviceslip.com/advice"
#define API_dog_fact "https://dogapi.dog/api/v2/facts?limit=1"
#define API_chuck_joke "https://geek-jokes.sameerkumar.website/api?format=json"
#define API_next_MCU "https://www.whenisthenextmcufilm.com/api"

//------------------------FUNCIONES--------------------------

const int DHCP_PACKET_CLIENT_ADDR_LEN_OFFSET = 2;
const int DHCP_PACKET_CLIENT_ADDR_OFFSET = 28;

enum State {
  READY,
  RECEIVING,
  RECIEVED
};

volatile State state = READY;
String newMAC;
String newIP;
String newName;
const char *hexDigits = "0123456789ABCDEF";
String DetectorMessage;

void parsePacket(char *data, int length) {
  if (state == RECIEVED) return;
  String tempName;
  String tempIP;
  String tempMAC;

  Serial.println("Just signed in:");
  //printHex(data, length);
  Serial.print("MAC address: ");
  // DetectorMessage+="MAC address:\n";
  for (int i = 0; i < data[DHCP_PACKET_CLIENT_ADDR_LEN_OFFSET]; i++)
    if (i < data[DHCP_PACKET_CLIENT_ADDR_LEN_OFFSET] - 1)
      Serial.printf("%02X:", (int)data[DHCP_PACKET_CLIENT_ADDR_OFFSET + i]);

    //  DetectorMessage+= (int)data[DHCP_PACKET_CLIENT_ADDR_OFFSET + i];
    else
      Serial.printf("%02X", (int)data[DHCP_PACKET_CLIENT_ADDR_OFFSET + i]);
  for (int i = 0; i < data[DHCP_PACKET_CLIENT_ADDR_LEN_OFFSET]; i++) {
    tempMAC += hexDigits[(int)data[DHCP_PACKET_CLIENT_ADDR_OFFSET + i] >> 4];
    tempMAC += hexDigits[(int)data[DHCP_PACKET_CLIENT_ADDR_OFFSET + i] & 15];
    if (i < data[DHCP_PACKET_CLIENT_ADDR_LEN_OFFSET] - 1)
      tempMAC += ":";
  }

  Serial.println();
  //parse options
  int opp = 240;
  while (opp < length) {
    switch (data[opp]) {
      case 0x0C:
        {
          Serial.print("Device name: ");
          DetectorMessage += "Device name:\n";
          for (int i = 0; i < data[opp + 1]; i++) {
            Serial.print(data[opp + 2 + i]);
            DetectorMessage += (data[opp + 2 + i]);
            tempName += data[opp + 2 + i];
          }
          Serial.println();
          break;
        }
      case 0x35:
        {
          Serial.print("Packet Type: ");
          switch (data[opp + 2]) {
            case 0x01:
              Serial.println("Discover");
              break;
            case 0x02:
              Serial.println("Offer");
              break;
            case 0x03:
              Serial.println("Request");
              if (state == READY)
                state = RECEIVING;
              break;
            case 0x05:
              Serial.println("ACK");
              break;
            default:
              Serial.println("Unknown");
          }
          break;
        }
      case 0x32:
        {
          Serial.print("Device IP: ");
          printIP(&data[opp + 2]);
          Serial.println();
          for (int i = 0; i < 4; i++) {
            tempIP += (int)data[opp + 2 + i];
            if (i < 3) tempIP += '.';
          }
          break;
        }
      case 0x36:
        {
          Serial.print("Server IP: ");
          printIP(&data[opp + 2]);
          Serial.println();
          break;
        }

      case 0xff:
        {
          opp = length;
          continue;
        }
      default:
        {
        }
    }

    opp += data[opp + 1] + 2;
  }
  if (state == RECEIVING) {
    newName = tempName;
    newIP = tempIP;
    newMAC = tempMAC;
    state = RECIEVED;
  }
  Serial.println();
  DetectorMessage = "Just accessed your network:\n";
  DetectorMessage += newName;
  DetectorMessage += "\nIP Address: ";
  DetectorMessage += newIP;
  DetectorMessage += "\nMac address: ";
  DetectorMessage += newMAC;
  DetectorMessage += "\n";
}

void setupUDP() {
  if (udps.listen(67)) {
    Serial.print("UDP Listening on IP: ");
    Serial.println(WiFi.localIP());
    udps.onPacket([](AsyncUDPPacket packet) {
      char *data = (char *)packet.data();
      int length = packet.length();
      parsePacket(data, length);
    });
  };
}

void printIP(char *data) {
  for(int i = 0; i < 4; i++)
  {
    Serial.print((int)data[i]);
 
    if(i < 3)
      Serial.print('.');
  }
}
void mandarMensaje(String from_name, String texto, String chat_id) {
  bot.sendMessage(chat_id, texto, "");
  bot.sendMessage(CHAT_LOG, texto, "");
  Serial.println(chat_id + texto);
}

void sendWOL(String MAC_ADDR) {
  WOL.sendMagicPacket(MAC_ADDR);  // send WOL on default port (9)
  delay(300);
}

//---------------------FUNCION RESPONDEDORA DE MENSAJES-------------------
void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = String(bot.messages[i].chat_id);

    // if (chat_id == CHAT_ADMIN){
    //   bot.sendMessage(chat_id, "Unauthorized user", "");
    //   continue;
    // }

    String text = bot.messages[i].text;

    Serial.println(text);
    String mensaje_para_enviar = text;
    String from_name = bot.messages[i].from_name;
    bot.sendMessage(CHAT_LOG, from_name + "(" + chat_id + "): " + mensaje_para_enviar, "");

    //------MANEJADOR DE RESPUESTAS---------------

    if (chat_id == CHAT_ADMIN) {
      if (text.startsWith("/wake_zima")) {
        sendWOL(MAC_ADDR_ZIMA);
        bot.sendMessage(chat_id, "ZIMA prendido :)", "");
      }

      if (text.startsWith("/wake_fierrito2")) {
        sendWOL(MAC_ADDR_FIERRITO2);
        bot.sendMessage(chat_id, "FIERRITO2 prendido :)", "");
      }

      if (text.startsWith("/is_zima_awake")) {
        IPAddress ip(192, 168, 0, 44);
        bool ret = Ping.ping(ip);
        if (ret) {
          bot.sendMessage(chat_id, "ZIMA IS AWAKE", "");
        } else {
          bot.sendMessage(chat_id, "ZIMA IS SLEEP... /wake_zima ?", "");
        }
      }
    }


    if (text.startsWith("/")) {

      if (text.startsWith("/start")) {
        String welcome = "Weeeelcomee to the INTERNET!\n\n";
        welcome += "/led_on to turn GPIO ON \n";
        welcome += "/led_off to turn GPIO OFF \n";
        welcome += "/led_state to request current GPIO state \n";
        welcome += "/msg send a message to the ADMIN \n";
        welcome += "/dev_joke send you a Programming Joke \n";
        welcome += "/dog_fact send you a random dog fact \n";
        welcome += "/cat_fact send you a random cat fact \n";
        welcome += "/chuck_joke send you a Chuck Norris Joke \n";
        welcome += "/next_MCU send you the next MCU event\n";
        welcome += "/advice send you a wise advice \n";
        welcome += "/zen_quote send you a relax quote \n";
        welcome += "/d-- send you the result of a dice roll of the amount you specified. E.g. /d20\n";
        welcome += "\n\n >> X713 <<";
        mandarMensaje(from_name, welcome, bot.messages[i].chat_id);
        continue;
      }


      if (text.startsWith("/led_on")) {
        digitalWrite(BUILTIN_LED, HIGH);
        bot.sendMessage(chat_id, "LED prendido :)", "");
      }

      if (text.startsWith("/led_off")) {
        digitalWrite(BUILTIN_LED, LOW);
        bot.sendMessage(chat_id, "LED apagado :(", "");
      }

      if (text.startsWith("/led_state")) {
        if (digitalRead(BUILTIN_LED)) {
          bot.sendMessage(chat_id, "El LED esta prendido :)", "");
        } else {
          bot.sendMessage(chat_id, "El LED esta apagado :(", "");
        }
      }

      if (text.startsWith("/msg")) {
        mensaje_para_enviar.remove(0, 5);
        bot.sendMessage(bot.messages[i].chat_id, "Se envio el mensaje: " + mensaje_para_enviar, "");
        bot.sendMessage(CHAT_ADMIN, "Se envio el mensaje: " + mensaje_para_enviar + "\nDe parte de: " + from_name, "");
      }

      if (text.startsWith("/d") && isDigit(text.charAt(2))) {

        int number = text.substring(2, 4).toInt();
        int diceValue = random(0, number) + 1;

        if (diceValue == 1) mensaje_para_enviar = "d" + text.substring(2, 4) + ": NAT 1";
        else if (diceValue == number) mensaje_para_enviar = "d" + text.substring(2, 4) + ": NAT " + number;
        else mensaje_para_enviar = "d" + text.substring(2, 4) + ": " + diceValue;

        mandarMensaje(from_name, mensaje_para_enviar, bot.messages[i].chat_id);
      }

#pragma region  // API CALLS
      if (text.startsWith("/dev_joke")) {
        JsonObject chiste;
        HTTPClient http;
        http.begin(API_Joke_Dev);
        String jsonBuffer;
        DynamicJsonDocument doc(1024);
        do {
          if (http.GET() > 0) {
            jsonBuffer = http.getString();
            http.end();
            deserializeJson(doc, jsonBuffer);
            chiste = doc.as<JsonObject>();
          }
        } while (chiste["type"] != "single");
        mensaje_para_enviar = chiste["joke"].as<String>();
        mandarMensaje(from_name, mensaje_para_enviar, bot.messages[i].chat_id);
      }

      if (text.startsWith("/chuck_joke")) {
        JsonObject dog_fact;
        HTTPClient http;
        http.begin(API_chuck_joke);
        String jsonBuffer;
        DynamicJsonDocument doc(1024);
        if (http.GET() > 0) {
          jsonBuffer = http.getString();
          http.end();
          deserializeJson(doc, jsonBuffer);
          dog_fact = doc.as<JsonObject>();
        }
        mensaje_para_enviar = dog_fact["joke"].as<String>();
        mandarMensaje(from_name, mensaje_para_enviar, bot.messages[i].chat_id);
      }

      if (text.startsWith("/next_MCU")) {
        JsonObject response;
        HTTPClient http;
        http.begin(API_next_MCU);
        String jsonBuffer;
        DynamicJsonDocument doc(1024);
        if (http.GET() > 0) {
          jsonBuffer = http.getString();
          http.end();
          deserializeJson(doc, jsonBuffer);
          response = doc.as<JsonObject>();
        }
        mensaje_para_enviar = "Next MCU event is " + response["title"].as<String>() + " in " + response["days_until"].as<String>() + " days";
        mandarMensaje(from_name, mensaje_para_enviar, bot.messages[i].chat_id);
      }

      if (text.startsWith("/dog_fact")) {
        JsonObject dog_fact;
        HTTPClient http;
        http.begin(API_dog_fact);
        String jsonBuffer;
        DynamicJsonDocument doc(1024);
        if (http.GET() > 0) {
          jsonBuffer = http.getString();
          http.end();
          deserializeJson(doc, jsonBuffer);
          dog_fact = doc.as<JsonObject>();
        }
        mensaje_para_enviar = dog_fact["data"].as<JsonArray>()[0].as<JsonObject>()["attributes"].as<JsonObject>()["body"].as<String>();
        mandarMensaje(from_name, mensaje_para_enviar, bot.messages[i].chat_id);
      }

      if (text.startsWith("/cat_fact")) {
        JsonObject chiste;
        HTTPClient http;
        http.begin(API_cat_fact);
        String jsonBuffer;
        DynamicJsonDocument doc(1024);
        if (http.GET() > 0) {
          jsonBuffer = http.getString();
          http.end();
          deserializeJson(doc, jsonBuffer);
          chiste = doc.as<JsonObject>();
        }
        mensaje_para_enviar = chiste["text"].as<String>();
        mandarMensaje(from_name, mensaje_para_enviar, bot.messages[i].chat_id);
      }

      if (text.startsWith("/zen_quote")) {
        JsonObject zen_quote;
        HTTPClient http;
        http.begin(API_zen_quotes);
        String jsonBuffer;
        DynamicJsonDocument doc(1024);
        if (http.GET() > 0) {
          jsonBuffer = http.getString();
          http.end();
          deserializeJson(doc, jsonBuffer);
          zen_quote = doc.as<JsonArray>()[0].as<JsonObject>();
        }
        mensaje_para_enviar = zen_quote["q"].as<String>() + " - " + zen_quote["a"].as<String>();
        mandarMensaje(from_name, mensaje_para_enviar, bot.messages[i].chat_id);
      }

      if (text.startsWith("/advice")) {
        JsonObject advice;
        HTTPClient http;
        http.begin(API_advice);
        String jsonBuffer;
        DynamicJsonDocument doc(1024);
        if (http.GET() > 0) {
          jsonBuffer = http.getString();
          http.end();
          deserializeJson(doc, jsonBuffer);
          advice = doc.as<JsonObject>();
        }
        mensaje_para_enviar = advice["slip"].as<JsonObject>()["advice"].as<String>();
        mandarMensaje(from_name, mensaje_para_enviar, bot.messages[i].chat_id);
      }

#pragma endregion  // API CALLS
    } else {
      bot.sendMessage(bot.messages[i].chat_id, "mmmmm... i dont get it. Can you try again?", "");
    }
  }
}

// void loopBis( void * parameter) {
//   for(;;){
//     //tarea 1 -> controlar el bot de telegram

//     delay(100);
//   }
// }

void setup() {
  Serial.begin(115200);

  pinMode(BUILTIN_LED, OUTPUT);
  digitalWrite(BUILTIN_LED, LOW);

  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, WIFI_PASS);
  clientTCP.setCACert(TELEGRAM_CERTIFICATE_ROOT);  // Add root certificate for api.telegram.org

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println(".");
  }
  Serial.println(WiFi.localIP());
  ultimoPrendido = millis();

  lastTimeBotRan = millis();

  // xTaskCreatePinnedToCore(
  //   loopBis, /* Function to implement the task */
  //   "Task1", /* Name of the task */
  //   10000,  /* Stack size in words */
  //   NULL,  /* Task input parameter */
  //   0,  /* Priority of the task */
  //   &Task1,  /* Task handle. */
  //   0); /* Core where the task should run */
  setupUDP();
  bot.sendMessage(CHAT_LOG, "> up & running");  
  Serial.println("--------------------------");
}

//loop que controla la pantalla
void loop() {

  if(state == RECIEVED)
	{	
	 bot.sendMessage(CHAT_ADMIN, DetectorMessage, "Markdown");
	 state = READY;
	}

  if (millis() > lastTimeBotRan + botRequestDelay) {
    // Serial.println("--------------------------");

    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    // Serial.println(numNewMessages);

    while (numNewMessages) {
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
    lastTimeBotRan = millis();
  }
}
