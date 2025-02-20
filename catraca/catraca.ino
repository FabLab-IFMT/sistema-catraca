#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <HardwareSerial.h>

// --- Configurações WiFi ---
const char* ssid = "Fabnet";
const char* password = "71037523";

// --- URL do servidor que hospeda o JSON ---
const char* serverUrl = "http://192.168.1.103:8080/cards.json";  // Atualize com o IP do seu computador

// --- Pinos e configuração do sensor RFID ---
#define RFID_RX_PIN 16
#define RFID_TX_PIN 17
HardwareSerial RFID(2);  // Utilizando UART2 do ESP32

// --- Pino de controle da catraca ---
// Estado normal: catraca energizada (HIGH) (mecanismo travado)
#define ACCESS_PIN 12

// --- Controle de leitura repetida ---
String lastCardProcessed = "";
unsigned long lastProcessedTime = 0;
const unsigned long processCooldown = 5000; // 5 segundos de cooldown

// --- Função de verificação de autorização ---
bool isCardAuthorized(const String &cardCode) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(serverUrl);
    int httpCode = http.GET();

    if (httpCode > 0) {
      if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        DynamicJsonDocument doc(1024);
        DeserializationError error = deserializeJson(doc, payload);
        if (error) {
          Serial.print("Erro ao fazer parse do JSON: ");
          Serial.println(error.c_str());
          http.end();
          return false;
        }
        JsonArray allowed_cards = doc["allowed_cards"].as<JsonArray>();
        for (JsonVariant card : allowed_cards) {
          if (card.as<String>() == cardCode) {
            http.end();
            return true;
          }
        }
      }
    } else {
      Serial.print("Erro na requisição HTTP: ");
      Serial.println(http.errorToString(httpCode));
    }
    http.end();
  }
  return false;
}

// --- Função para liberar a catraca: desenergiza temporariamente o mecanismo ---
void grantAccess() {
  Serial.println("Acesso concedido! Desenergizando catraca.");
  digitalWrite(ACCESS_PIN, LOW);   // Desenergiza para liberar o mecanismo
  delay(3000);                     // Tempo para a passagem (3 segundos)
  digitalWrite(ACCESS_PIN, HIGH);  // Reenergiza, travando novamente a catraca
}

void setup() {
  Serial.begin(115200);

  // Inicializa o sensor RFID
  RFID.begin(9600, SERIAL_8N1, RFID_RX_PIN, RFID_TX_PIN);

  // Configura o pino de controle da catraca como saída
  pinMode(ACCESS_PIN, OUTPUT);
  digitalWrite(ACCESS_PIN, HIGH);  // Estado normal: catraca energizada (travada)

  // Conecta ao WiFi
  Serial.print("Conectando ao WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" Conectado!");
  Serial.print("IP do ESP32: ");
  Serial.println(WiFi.localIP());

  Serial.println("Aguardando leitura do sensor RFID (RDM6300)...");
}

void loop() {
  // Verifica se há dados disponíveis do sensor RFID
  if (RFID.available() > 0) {
    if (RFID.peek() == 0x02) {
      if (RFID.available() >= 14) {
        byte frame[14];
        for (int i = 0; i < 14; i++) {
          frame[i] = RFID.read();
        }
        if (frame[0] == 0x02 && frame[13] == 0x03) {
          char tagCode[11];  // 10 caracteres + '\0'
          memcpy(tagCode, frame + 1, 10);
          tagCode[10] = '\0';
          String cardCode = String(tagCode);
          Serial.print("Tag lida: ");
          Serial.println(cardCode);

          // Verifica se o mesmo cartão foi processado recentemente
          unsigned long currentTime = millis();
          if (cardCode == lastCardProcessed && (currentTime - lastProcessedTime) < processCooldown) {
            // Se estiver dentro do cooldown, ignora a leitura
            return;
          }
          lastCardProcessed = cardCode;
          lastProcessedTime = currentTime;
          
          // Verifica autorização e atua na catraca
          if (isCardAuthorized(cardCode)) {
            grantAccess();
          } else {
            Serial.println("Acesso negado.");
          }
        } else {
          Serial.println("Frame inválido.");
        }
      }
    } else {
      RFID.read(); // Descarte caso não seja o byte inicial esperado
    }
  }
}
