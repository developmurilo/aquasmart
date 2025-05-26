#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>

#define POT_PIN 34      // Pino do potenciômetro
#define RELAY_PIN 26    // Pino do relé
#define LED_PIN 27      // Pino do LED
#define SERVO_PIN 18    // Pino PWM do servo
#define MQTT_SERVER "broker.emqx.io" // Broker MQTT

// Parâmetros para filtragem de leitura do sensor
#define AMOSTRAS 3
int leituras[AMOSTRAS];
int indiceAtual = 0;
int totalLeituras = 0;

Servo valveServo;
WiFiClient espClient;
PubSubClient mqttClient(espClient);

const char* ssid = "Wokwi-GUEST";  // Nome da rede WiFi
const char* password = "";         // Senha do WiFi

// Configuração MQTT
const char* MQTT_CLIENT_ID = "ESP32Client_"; // Será concatenado com um número aleatório
const char* TOPIC_ALERTA = "agua/alerta";  // Tópico de alerta MQTT

// Variável para controle de estado
bool estadoAnterior = false;

void setup() {
  Serial.begin(115200);

  pinMode(POT_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);

  valveServo.attach(SERVO_PIN, 500, 2400); // Define PWM para o servo (500-2400 µs)
  digitalWrite(RELAY_PIN, LOW); // Relé desligado
  digitalWrite(LED_PIN, LOW);   // LED desligado
  valveServo.write(0);          // Servo na posição inicial

  // Inicializa o array de leituras
  for (int i = 0; i < AMOSTRAS; i++) {
    leituras[i] = 0;
  }

  // Conexão com a rede WiFi com timeout
  Serial.print("Conectando ao WiFi");
  WiFi.begin(ssid, password);
  
  unsigned long startTime = millis();
  // Espera até 20 segundos pela conexão
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 20000) {
    delay(500);
    Serial.print(".");
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nFalha na conexão WiFi! Reiniciando...");
    ESP.restart();
  } else {
    Serial.println("\nWiFi conectado!");
    Serial.print("Endereço IP: ");
    Serial.println(WiFi.localIP());
  }

  // Configuração do cliente MQTT com ID único
  String clientId = MQTT_CLIENT_ID + String(random(0xffff), HEX);
  mqttClient.setServer(MQTT_SERVER, 1883);
  mqttClient.setCallback(mqttCallback);
  
  // Tentativa inicial de conexão MQTT
  reconnect();
}

// Função para filtrar leituras do sensor
int lerSensorComFiltro() {
  totalLeituras -= leituras[indiceAtual];
  leituras[indiceAtual] = analogRead(POT_PIN);
  totalLeituras += leituras[indiceAtual];
  indiceAtual = (indiceAtual + 1) % AMOSTRAS;
  
  return totalLeituras / AMOSTRAS;
}

void loop() {
  // Verifica WiFi e reconecta se necessário
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi desconectado. Reconectando...");
    WiFi.reconnect();
    delay(5000);
    
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Falha ao reconectar WiFi. Reiniciando...");
      ESP.restart();
      return;
    }
  }
  
  // Verifica conexão MQTT
  if (!mqttClient.connected()) {
    reconnect();
  }
  mqttClient.loop();  // Processa mensagens recebidas

  // Lê o valor do potenciômetro com filtro
  int potValue = lerSensorComFiltro();
  int percent = map(potValue, 0, 4095, 0, 100); // Converte para porcentagem

  Serial.print("Fluxo de Água: ");
  Serial.print(percent);
  Serial.println("%");

  // Lógica de controle da válvula
  bool estadoAtual = (percent > 70);
  
  if (estadoAtual) {  
    digitalWrite(RELAY_PIN, HIGH);  // Ativa o relé
    digitalWrite(LED_PIN, HIGH);    // Acende o LED
    valveServo.write(90);           // Fecha a válvula (servo)

    Serial.println("🚨 ALERTA: Fluxo alto! Fechando válvula...");
    
    // Publica apenas se o estado mudou
    if (estadoAtual != estadoAnterior) {
      String mensagem = "ALERTA: Fluxo alto (" + String(percent) + "%)! Válvula fechada!";
      mqttClient.publish(TOPIC_ALERTA, mensagem.c_str());
    }
  } else {
    digitalWrite(RELAY_PIN, LOW);   // Desativa o relé
    digitalWrite(LED_PIN, LOW);     // Apaga o LED
    valveServo.write(0);            // Abre a válvula (servo)

    Serial.println("✅ Fluxo normal. Válvula aberta.");
    
    // Publica apenas se o estado mudou
    if (estadoAtual != estadoAnterior) {
      String mensagem = "Fluxo normal (" + String(percent) + "%). Válvula aberta.";
      mqttClient.publish(TOPIC_ALERTA, mensagem.c_str());
    }
  }
  
  // Atualiza o estado anterior
  estadoAnterior = estadoAtual;

  delay(500);  // Atraso para estabilidade
}

void reconnect() {
  int tentativas = 0;
  // Tenta reconectar ao MQTT até 5 vezes
  while (!mqttClient.connected() && tentativas < 5) {
    Serial.print("Conectando ao MQTT...");
    
    // Gera um ID de cliente único
    String clientId = MQTT_CLIENT_ID + String(random(0xffff), HEX);
    
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println(" Conectado!");
      
      // Inscreve no tópico
      boolean subscribeSuccess = mqttClient.subscribe(TOPIC_ALERTA);
      Serial.print("Inscrição no tópico: ");
      Serial.println(subscribeSuccess ? "Sucesso" : "Falha");
      
      // Envia mensagem de status inicial
      mqttClient.publish(TOPIC_ALERTA, "Dispositivo online");
    } else {
      Serial.print(" Falha, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" Tentando novamente em 5s...");
      tentativas++;
      delay(5000);  // Espera 5 segundos antes de tentar reconectar
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Converter payload para string para facilitar manipulação
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.println("------------------------------");
  Serial.print("Tópico: ");
  Serial.println(topic);
  Serial.print("Mensagem: ");
  Serial.println(message);
  Serial.println("------------------------------");
  
  // Processar comandos remotos
  if (message.indexOf("RESET") >= 0) {
    Serial.println("Comando de reset recebido. Reiniciando...");
    ESP.restart();
  }
}