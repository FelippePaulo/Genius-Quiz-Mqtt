#include <SPI.h>
#include <Ethernet.h>
#include <PubSubClient.h>

// Configuração do cliente MQTT
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };  // Endereço MAC (ajuste conforme necessário)
IPAddress ip(192, 168, 1, 177);                      // Endereço IP estático do Arduino
const char* mqtt_server = "test.mosquitto.org";
const int mqtt_port = 1883;  // Porta não segura

const char* mqtt_client_id = "arduinoClient";

// Inicializa Ethernet e PubSubClient
EthernetClient ethClient;
PubSubClient client(ethClient);

// Configuração dos LEDs, botões e sons
#define NOTE_D4  294
#define NOTE_G4  392
#define NOTE_A4  440
#define NOTE_A5  880

int tons[4] = { NOTE_A5, NOTE_A4, NOTE_G4, NOTE_D4 };
int sequencia[100] = {};
int rodada_atual = 0;
int passo_atual_na_sequencia = 0;
int pinoAudio = 12;
int pinosLeds[4] = { 2, 4, 6, 8 };
int pinosBotoes[4] = { 3, 5, 7, 9 };
int botao_pressionado = 0;
int perdeu_o_jogo = false;
int vitorias_consecutivas = 0;  // Contador de vitórias consecutivas

void setup() {
  Serial.begin(115200);

  // Inicializa a conexão Ethernet
  if (Ethernet.begin(mac) == 0) {
    Serial.println("Falha ao configurar via DHCP");
    Ethernet.begin(mac, ip);  // Tenta com IP estático se o DHCP falhar
  }
  Serial.print("Endereço IP atribuído: ");
  Serial.println(Ethernet.localIP());

  // Define o servidor MQTT
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);  // Configura o callback para mensagens recebidas

  // Define os modos dos pinos
  for (int i = 0; i <= 3; i++) {
    pinMode(pinosLeds[i], OUTPUT);
    pinMode(pinosBotoes[i], INPUT_PULLUP);
  }
  pinMode(pinoAudio, OUTPUT);

  // Inicializando o random através de uma leitura da porta analógica
  randomSeed(analogRead(0));
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  if (perdeu_o_jogo) {
    int sequencia[100] = {};
    rodada_atual = 0;
    passo_atual_na_sequencia = 0;
    perdeu_o_jogo = false;
    vitorias_consecutivas = 0; // Reseta o contador de vitórias ao perder
  }

  if (rodada_atual == 0) {
    tocarSomDeInicio();
    delay(500);
  }

  proximaRodada();
  reproduzirSequencia();
  aguardarJogador();

  delay(1000);
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Tentando conectar ao MQTT...");
    if (client.connect(mqtt_client_id, mqtt_user, mqtt_password)) {
      Serial.println("Conectado com sucesso!");
    } else {
      Serial.print("Falha na conexão, rc=");
      Serial.print(client.state());
      Serial.println(" Tentando novamente em 5 segundos");
      delay(5000);
    }
  }
}

void proximaRodada() {
  int numero_sorteado = random(0, 4);
  sequencia[rodada_atual++] = numero_sorteado;
}

void reproduzirSequencia() {
  for (int i = 0; i < rodada_atual; i++) {
    tone(pinoAudio, tons[sequencia[i]]);
    digitalWrite(pinosLeds[sequencia[i]], HIGH);
    delay(500);
    noTone(pinoAudio);
    digitalWrite(pinosLeds[sequencia[i]], LOW);
    delay(100);
  }
  noTone(pinoAudio);
}

void aguardarJogador() {
  for (int i = 0; i < rodada_atual; i++) {
    aguardarJogada();

    if (sequencia[passo_atual_na_sequencia] != botao_pressionado) {
      gameOver();
      return;  // Encerra a função se o jogo terminar
    }

    passo_atual_na_sequencia++;
  }

  passo_atual_na_sequencia = 0;
  vitorias_consecutivas++;
  enviarVitoriasMQTT(vitorias_consecutivas);  // Envia o número de vitórias para o servidor MQTT
}

void aguardarJogada() {
  boolean jogada_efetuada = false;
  while (!jogada_efetuada) {
    for (int i = 0; i <= 3; i++) {
      if (!digitalRead(pinosBotoes[i])) {
        botao_pressionado = i;

        tone(pinoAudio, tons[i]);
        digitalWrite(pinosLeds[i], HIGH);
        delay(300);
        digitalWrite(pinosLeds[i], LOW);
        noTone(pinoAudio);

        jogada_efetuada = true;
      }
    }
    delay(10);
  }
}

void gameOver() {
  for (int i = 0; i <= 3; i++) {
    tone(pinoAudio, tons[i]);
    digitalWrite(pinosLeds[i], HIGH);
    delay(200);
    digitalWrite(pinosLeds[i], LOW);
    noTone(pinoAudio);
  }

  tone(pinoAudio, tons[3]);
  for (int i = 0; i <= 3; i++) {
    digitalWrite(pinosLeds[0], HIGH);
    digitalWrite(pinosLeds[1], HIGH);
    digitalWrite(pinosLeds[2], HIGH);
    digitalWrite(pinosLeds[3], HIGH);
    delay(100);
    digitalWrite(pinosLeds[0], LOW);
    digitalWrite(pinosLeds[1], LOW);
    digitalWrite(pinosLeds[2], LOW);
    digitalWrite(pinosLeds[3], LOW);
    delay(100);
  }
  noTone(pinoAudio);

  perdeu_o_jogo = true;  
}

void tocarSomDeInicio() {
  tone(pinoAudio, tons[0]);
  digitalWrite(pinosLeds[0], HIGH);
  digitalWrite(pinosLeds[1], HIGH);
  digitalWrite(pinosLeds[2], HIGH);
  digitalWrite(pinosLeds[3], HIGH);
  delay(500);
  digitalWrite(pinosLeds[0], LOW);
  digitalWrite(pinosLeds[1], LOW);
  digitalWrite(pinosLeds[2], LOW);
  digitalWrite(pinosLeds[3], LOW);
  delay(500);
  noTone(pinoAudio);
}

void enviarVitoriasMQTT(int vitorias) {
  char mensagem[50];
  sprintf(mensagem, "Vitórias consecutivas: %d", vitorias);
  client.publish("jogo_memoria/vitorias", mensagem);
}

// Callback para processar mensagens recebidas (se necessário)
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Mensagem recebida no tópico [");
  Serial.print(topic);
  Serial.print("]: ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}
