#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <HardwareSerial.h>

// --- Configurações WiFi ---
const char* ssid = "Fabnet";
const char* password = "71037523";

// --- URL do servidor Django ---
const char* serverUrl = "http://192.168.1.103:8000/acesso_e_ponto/verificar_cartao/";  // Atualize com o IP do seu servidor Django

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

// --- Função de verificação de autorização diretamente no Django ---
bool isCardAuthorized(const String &cardCode) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/json");

    // Criando JSON para enviar ao servidor
    DynamicJsonDocument doc(200);
    doc["card_number"] = cardCode;
    String requestBody;
    serializeJson(doc, requestBody);

    int httpCode = http.POST(requestBody);

    if (httpCode == HTTP_CODE_OK) {
      String response = http.getString();
      DynamicJsonDocument responseDoc(200);
      DeserializationError error = deserializeJson(responseDoc, response);

      if (!error) {
        bool authorized = responseDoc["authorized"];
        String message = responseDoc["message"].as<String>();
        Serial.print("Resposta do servidor: ");
        Serial.println(message);
        http.end();
        return authorized;
      } else {
        Serial.println("Erro ao interpretar JSON de resposta");
      }
    } else {
      Serial.printf("Erro HTTP: %d\n", httpCode);
      Serial.println(http.getString());
    }
    http.end();
  } else {
    Serial.println("Erro: WiFi não conectado");
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
          
          // Verifica autorização no Django e atua na catraca
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
