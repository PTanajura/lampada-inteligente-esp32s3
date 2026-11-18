#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>

// ==========================================================
// CONFIGURAÇÕES DE REDE E SERVIDOR LOCAL
// ==========================================================
const char* SSID     = ""; // Mudar pelo nome da rede wifi
const char* PASSWORD = ""; // Mudar pela senha da rede wifi

// IPv4 do PC que executa o api_banco.py
const String SERVER_BASE = ""; // Mudar pelo endereço do banco

// ==========================================================
// PINOS DO HARDWARE (ESP32-WROOM-32D)
// ==========================================================
const int PINO_TEMP   = 4;  // Módulo DHT22 -> pino 'out' no D4 ('+' no VIN/3V3, '-' no GND)
const int PINO_LDR    = 34; // Fotorresistor / sensor de luz (ADC1: 0 a 4095)
const int PINO_POT    = 32; // Potenciômetro / cor da lâmpada (ADC1: 0 a 4095)
const int PINO_BOTAO  = 26; // Botão físico (interrupção externa)
const int PINO_BUZZER = 14; // Buzzer de alerta (Saída)
const int PINO_LED_R  = 33; // LED RGB - Vermelho (PWM)
const int PINO_LED_G  = 25; // LED RGB - Verde (PWM)
const int PINO_LED_B  = 27; // LED RGB - Azul (PWM)

#define DHTTYPE DHT22
DHT dht(PINO_TEMP, DHTTYPE);

// ==========================================================
// PARÂMETROS DE CALIBRAÇÃO
// ==========================================================
const bool LED_ANODO_COMUM   = false; // 'false' = Cátodo Comum (pino comum no GND)
const int LIMITE_LUZ_ESCURO  = 1600;  // Abaixo de 1600 (escala 0-4095) considera escuro
const float TEMP_MIN_SEGURA  = 0.0;   // Abaixo de 0 °C -> Perigo!
const float TEMP_MAX_SEGURA  = 25.0;  // Acima de 25 °C -> Perigo!

// ==========================================================
// VARIÁVEIS DE ESTADO E INTERRUPÇÃO
// ==========================================================
volatile bool sistemaLigado = true;
volatile unsigned long ultimaInterrupcaoBotao = 0;

bool temperaturaAtiva = true;
bool detectorAtivo    = true;
bool buzzerAtivo      = true;

float temperaturaAtual = 22.0;
bool dhtLeituraOk      = false;

// Controle de cor temporária (1 segundo) via comando online
String corTemporaria = "";
unsigned long fimCorTemporaria = 0;

// Controle de tempo não-bloqueante
unsigned long ultimoCicloLocal = 0;
unsigned long ultimaLeituraDHT = 0;
unsigned long ultimoCicloRede  = 0;

const unsigned long INTERVALO_LOCAL = 250;  // Resposta rápida (potenciômetro/LED/Serial)
const unsigned long INTERVALO_DHT   = 2500; // Intervalo seguro para o DHT22 (2,5s)
const unsigned long INTERVALO_REDE  = 3000; // Envio HTTP para o SQLite a cada 3s

// ==========================================================
// INTERRUPÇÃO EXTERNA DO BOTÃO (LIGA/DESLIGA LED E BUZZER)
// ==========================================================
void IRAM_ATTR isrBotao() {
  unsigned long agora = millis();
  if (agora - ultimaInterrupcaoBotao > 250) {
    sistemaLigado = !sistemaLigado;
    ultimaInterrupcaoBotao = agora;
  }
}

// ==========================================================
// CONTROLE PWM DO LED RGB E ESPECTRO DO POTENCIÔMETRO
// ==========================================================
void escreverRGB(int r, int g, int b) {
  r = constrain(r, 0, 255);
  g = constrain(g, 0, 255);
  b = constrain(b, 0, 255);

  if (LED_ANODO_COMUM) {
    r = 255 - r;
    g = 255 - g;
    b = 255 - b;
  }

  analogWrite(PINO_LED_R, r);
  analogWrite(PINO_LED_G, g);
  analogWrite(PINO_LED_B, b);
}

// Espectro: Vermelho -> Laranja -> Branco -> Azul Turquesa -> Azul
String calcularCorPotenciometro(int leituraPot, int &r, int &g, int &b) {
  if (leituraPot < 1024) {
    r = 255;
    g = map(leituraPot, 0, 1023, 0, 100);
    b = 0;
    return (leituraPot < 512) ? "Vermelho" : "Laranja";
  } 
  else if (leituraPot < 2048) {
    r = 255;
    g = map(leituraPot, 1024, 2047, 100, 255);
    b = map(leituraPot, 1024, 2047, 0, 255);
    return (leituraPot < 1536) ? "Laranja" : "Branco";
  } 
  else if (leituraPot < 3072) {
    r = map(leituraPot, 2048, 3071, 255, 0);
    g = 255;
    b = map(leituraPot, 2048, 3071, 255, 220);
    return (leituraPot < 2560) ? "Branco" : "Azul Turquesa";
  } 
  else {
    r = 0;
    g = map(leituraPot, 3072, 4095, 255, 0);
    b = map(leituraPot, 3072, 4095, 220, 255);
    return (leituraPot < 3584) ? "Azul Turquesa" : "Azul";
  }
}

// ==========================================================
// COMUNICAÇÃO HTTP COM A API LOCAL (SQLITE / CLOUD)
// ==========================================================
String extrairCampoJson(String json, String chave) {
  String busca = "\"" + chave + "\":\"";
  int inicio = json.indexOf(busca);
  if (inicio == -1) return "";
  inicio += busca.length();
  int fim = json.indexOf("\"", inicio);
  if (fim == -1) return "";
  return json.substring(inicio, fim);
}

void processarComando(String cmd) {
  cmd.trim();
  if (cmd == "") return;

  Serial.println("[PAINEL ONLINE] Comando recebido: " + cmd);

  if (cmd.equalsIgnoreCase("Ligar")) {
    sistemaLigado = true;
  } 
  else if (cmd.equalsIgnoreCase("Desligar")) {
    sistemaLigado = false;
    corTemporaria = "";
  } 
  else if (cmd.equalsIgnoreCase("Vermelho") || 
           cmd.equalsIgnoreCase("Amarelo")  || 
           cmd.equalsIgnoreCase("Azul")) {
    corTemporaria = cmd;
    corTemporaria[0] = toupper(corTemporaria[0]);
    fimCorTemporaria = millis() + 1000;
  } 
  else if (cmd.equalsIgnoreCase("Desativar Temperatura")) {
    temperaturaAtiva = false;
  } 
  else if (cmd.equalsIgnoreCase("Ativar Temperatura")) {
    temperaturaAtiva = true;
  } 
  else if (cmd.equalsIgnoreCase("Desativar Detector")) {
    detectorAtivo = false;
  } 
  else if (cmd.equalsIgnoreCase("Ativar Detector")) {
    detectorAtivo = true;
  } 
  else if (cmd.equalsIgnoreCase("Desativar Buzzer")) {
    buzzerAtivo = false;
  } 
  else if (cmd.equalsIgnoreCase("Ativar Buzzer")) {
    buzzerAtivo = true;
  }
}

void verificarComandosAPI() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(SERVER_BASE + "/api/comando");
  int httpCode = http.GET();

  if (httpCode == 200) {
    String payload = http.getString();
    String cmd = extrairCampoJson(payload, "comando");
    processarComando(cmd);
  }
  http.end();
}

void enviarTelemetriaAPI(float temp, int lum, String cor, bool botao) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(SERVER_BASE + "/api/telemetria");
  http.addHeader("Content-Type", "application/json");

  String json = "{";
  json += "\"temperatura\":" + String(temp, 1) + ",";
  json += "\"luminosidade\":" + String(lum) + ",";
  json += "\"cor_led\":\"" + cor + "\",";
  json += "\"botao\":" + String(botao ? "true" : "false");
  json += "}";

  int httpCode = http.POST(json);
  if (httpCode <= 0) {
    Serial.println("[ERRO POST API] Código: " + String(httpCode));
  }
  http.end();
}

// ==========================================================
// SETUP E LOOP PRINCIPAL
// ==========================================================
void setup() {
  Serial.begin(115200);

  dht.begin();

  pinMode(PINO_BOTAO, INPUT_PULLUP);
  pinMode(PINO_BUZZER, OUTPUT);
  pinMode(PINO_LED_R, OUTPUT);
  pinMode(PINO_LED_G, OUTPUT);
  pinMode(PINO_LED_B, OUTPUT);

  digitalWrite(PINO_BUZZER, LOW);
  escreverRGB(0, 0, 0);

  attachInterrupt(digitalPinToInterrupt(PINO_BOTAO), isrBotao, FALLING);

  WiFi.begin(SSID, PASSWORD);
  Serial.print("Conectando ao Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi conectado! IP ESP32: " + WiFi.localIP().toString());
  delay(2000);
}

void loop() {
  unsigned long agora = millis();

  // 1. LEITURA DO MÓDULO DHT22 A CADA 2,5 SEGUNDOS
  if (agora - ultimaLeituraDHT >= INTERVALO_DHT) {
    ultimaLeituraDHT = agora;
    float leitura = dht.readTemperature();

    if (!isnan(leitura)) {
      temperaturaAtual = leitura;
      dhtLeituraOk = true;
    } else {
      dhtLeituraOk = false;
      Serial.println("[AVISO DHT22] Falha na leitura (verifique '+' no VIN/3V3, '-' no GND e 'out' no D4).");
    }
  }

  // 2. CICLO LOCAL RÁPIDO (250 ms)
  if (agora - ultimoCicloLocal >= INTERVALO_LOCAL) {
    ultimoCicloLocal = agora;

    // Leitura bruta do Fotorresistor (0 a 4095)
    int luminosidade = analogRead(PINO_LDR);

    // Leitura do Potenciômetro
    int rawPot = analogRead(PINO_POT);
    int r = 0, g = 0, b = 0;
    String nomeCorPot = calcularCorPotenciometro(rawPot, r, g, b);

    // Avaliação das condições
    bool tempPerigosa = temperaturaAtiva && (temperaturaAtual < TEMP_MIN_SEGURA || temperaturaAtual > TEMP_MAX_SEGURA);
    bool ambienteEscuro = (!detectorAtivo) || (luminosidade < LIMITE_LUZ_ESCURO);
    bool emTesteCor1s = (corTemporaria != "" && agora < fimCorTemporaria);

    // Controle do Buzzer
    if (sistemaLigado && buzzerAtivo && tempPerigosa) {
      tone(PINO_BUZZER, 2000);
    } else {
      noTone(PINO_BUZZER);
      digitalWrite(PINO_BUZZER, LOW);
    }

    // Controle do LED RGB
    bool ledLigado = false;
    String corExibida = "Desligado";

    if (!sistemaLigado || tempPerigosa) {
      ledLigado = false;
      corExibida = "Desligado";
      escreverRGB(0, 0, 0);
    } 
    else if (emTesteCor1s) {
      ledLigado = true;
      corExibida = corTemporaria;
      if (corTemporaria == "Vermelho") escreverRGB(255, 0, 0);
      else if (corTemporaria == "Amarelo") escreverRGB(255, 180, 0);
      else if (corTemporaria == "Azul") escreverRGB(0, 0, 255);
    } 
    else if (ambienteEscuro) {
      ledLigado = true;
      corExibida = nomeCorPot;
      escreverRGB(r, g, b);
    } 
    else {
      ledLigado = false;
      corExibida = "Desligado";
      escreverRGB(0, 0, 0);
    }

    // Monitor Serial
    Serial.print("LED RGB: ");
    Serial.print(ledLigado ? "ON (ligado)" : "OFF (desligado)");
    Serial.print(" | Cor: ");
    Serial.print(corExibida);
    Serial.print(" | Temp: ");
    Serial.print(temperaturaAtual, 1);
    Serial.print(dhtLeituraOk ? " °C" : " °C (aguardando DHT)");
    Serial.print(" | Luz (ADC): ");
    Serial.println(luminosidade);

    if (tempPerigosa) {
      Serial.println("Perigo! Desligar!");
    }

    // 3. CICLO DE REDE (A cada 3s)
    if (agora - ultimoCicloRede >= INTERVALO_REDE) {
      ultimoCicloRede = agora;
      verificarComandosAPI();
      enviarTelemetriaAPI(temperaturaAtual, luminosidade, corExibida, sistemaLigado);
    }
  }
}