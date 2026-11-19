// Sentinela Pirajuçara: nó de borda para alerta de alagamento em córrego urbano.
// ESP32 + HC-SR04 (nível), potenciômetro (pluviômetro simulado), DHT22, botão,
// LCD I2C, LED RGB, buzzer e servo (cancela da via). Comunicação: HiveMQ Cloud (TLS)
// e ThingSpeak (HTTP).
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHTesp.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <time.h>

#include "borda.h"
#include "secrets.h"

using namespace borda;

// ---------------- Pinos ----------------
const int PINO_TRIG = 5;
const int PINO_ECHO = 18;
const int PINO_DHT = 15;
const int PINO_CHUVA = 34;      // potenciômetro simulando o pluviômetro (declarado no relatório)
const int PINO_BOTAO = 4;       // silenciar sirene no local
const int PINO_QUEDA = 27;      // chave de teste: simula queda do link de rede
// A posição da chave no boot vale como "rede normal"; mudar de posição derruba o link.
// Assim o teste não depende de qual lado da chave o Wokwi considera inicial.
int posicaoChaveBoot = HIGH;
const int PINO_LED_R = 25;
const int PINO_LED_G = 26;
const int PINO_LED_B = 33;
const int PINO_BUZZER = 13;
const int PINO_SERVO = 19;

// Canais LEDC em timers diferentes, porque buzzer e servo usam frequências diferentes
const int CANAL_SERVO = 4;      // timer 2
const int CANAL_BUZZER = 6;     // timer 3

// ---------------- Instalação ----------------
// Sensor preso sob a ponte, 350 cm acima do leito. O canal transborda em 300 cm.
const float ALTURA_SENSOR_CM = 350.0f;
const float CHUVA_MAX_MM_H = 100.0f;  // fundo de escala do potenciômetro

// ---------------- Tempos ----------------
const uint32_t PERIODO_ULTRASSOM_MS = 500;
const uint32_t PERIODO_CHUVA_MS = 500;
const uint32_t PERIODO_DHT_MS = 2500;
const uint32_t PERIODO_TAXA_MS = 5000;
const uint32_t PERIODO_LCD_MS = 500;
const uint32_t PERIODO_THINGSPEAK_MS = 20000;  // plano gratuito aceita 1 envio a cada 15 s
const uint32_t PERIODO_DIAGNOSTICO_MS = 300000;
const uint32_t HEARTBEAT_TELEMETRIA_MS = 300000;
const float BANDA_MORTA_CM = 2.0f;
const uint32_t WIFI_TIMEOUT_TENTATIVA_MS = 15000;
const uint32_t MQTT_BACKOFF_INICIAL_MS = 2000;
const uint32_t MQTT_BACKOFF_MAXIMO_MS = 30000;
const uint16_t MQTT_KEEPALIVE_S = 10;

// ---------------- Tópicos ----------------
#define GRUPO "gs-gm"
#define LOCAL "pirajucara-eliseu"
#define NO_ID "no01"
#define BASE "citizentech/" GRUPO "/" LOCAL "/" NO_ID "/"
const char* T_NIVEL = BASE "nivel";
const char* T_CHUVA = BASE "chuva";
const char* T_AMBIENTE = BASE "ambiente";
const char* T_ESTADO = BASE "estado";
const char* T_STATUS = BASE "status";
const char* T_DIAG = BASE "diagnostico";
const char* T_ACK = BASE "ack";
const char* T_CMD = BASE "cmd";
const char* FW_VERSAO = "1.0.0";

// ---------------- Objetos ----------------
WiFiClientSecure clienteTls;
PubSubClient mqtt(clienteTls);
DHTesp dht;
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ---------------- Estado da borda ----------------
Limiares limiares;
Antecipacao antecipacao;
MediaMovel<8> filtroNivel;
MediaMovel<4> filtroChuva;
HistoricoTaxa<13> historicoTaxa;  // 13 pontos a cada 5 s = janela de 60 s
Agregador aggNivel, aggChuva, aggTemp, aggUmid;

float nivelFiltrado = 0, chuvaFiltrada = 0, taxaSubida = 0;
float temperatura = 25.0f, umidade = 60.0f;
Estado estadoNivel = NORMAL;      // estado só pelo nível (com histerese)
Estado estadoAtual = NORMAL;      // decisão final, após antecipação
Motivo motivoAtual = MOTIVO_NIVEL;
bool estadoPendente = true;       // evento de estado ainda não publicado

enum ModoCancela : uint8_t { CANCELA_AUTO, CANCELA_FECHAR, CANCELA_ABRIR };
ModoCancela modoCancela = CANCELA_AUTO;
bool cancelaFechada = false;
bool sireneSilenciada = false;
Estado estadoQuandoSilenciou = NORMAL;

// Janela agregada: é o que vai para o MQTT e o que fica no buffer durante a queda
struct Janela {
  uint32_t ts;          // epoch (s) se o NTP sincronizou, senão uptime (s)
  bool tsReal;
  bool offline;         // gerada com o link fora do ar
  float nivelMed, nivelMin, nivelMax, taxa;
  float chuvaMed, chuvaMax;
  float temp, umid;
  uint8_t estado;
  uint16_t amostras;
};
FilaCircular<Janela, 60> buffer;  // 60 janelas de 30 s = 30 min de queda sem perder dado

uint32_t intervaloJanelaMs = 30000;
uint32_t amostrasBrutasJanela = 0;
float ultimoNivelPublicado = 0;
bool jaPublicouTelemetria = false;
uint32_t instanteUltimaTelemetria = 0;

// Contadores para o diagnóstico e para a análise de escala
uint32_t totalAmostras = 0, totalPublicadas = 0, totalEvitadas = 0;
uint32_t reconexoesMqtt = 0, errosUltrassom = 0;

// Rede
bool quedaSimulada = false;
bool wifiTentando = false;
uint32_t inicioTentativaWifi = 0;
bool wifiEstavaConectado = false;
bool ntpConfigurado = false;
uint32_t proximaTentativaMqtt = 0;
uint32_t backoffMqtt = MQTT_BACKOFF_INICIAL_MS;

// Temporizadores
uint32_t tUltrassom = 0, tChuva = 0, tDht = 0, tTaxa = 0, tLcd = 0;
uint32_t tJanela = 0, tThingSpeak = 0, tDiag = 0;

// ---------------- Utilitários ----------------
double r1(float v) { return roundf(v * 10.0f) / 10.0; }

bool horaValida() { return time(nullptr) > 1700000000; }

uint32_t carimboTempo(bool& real) {
  real = horaValida();
  return real ? (uint32_t)time(nullptr) : millis() / 1000;
}

bool linkOk() { return !quedaSimulada && WiFi.status() == WL_CONNECTED && mqtt.connected(); }

void anexarTempo(JsonDocument& doc, uint32_t ts, bool real) {
  if (real) doc["ts"] = ts; else doc["uptime_s"] = ts;
}

bool publicarJson(const char* topico, JsonDocument& doc, bool retido = false) {
  char buf[384];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  if (!mqtt.connected()) return false;
  bool ok = mqtt.publish(topico, (const uint8_t*)buf, n, retido);
  if (ok) totalPublicadas++;
  return ok;
}

// ---------------- Atuadores ----------------
void escreverServo(int graus) {
  uint32_t us = 500 + (uint32_t)graus * 1900 / 180;
  ledcWrite(CANAL_SERVO, us * 65535UL / 20000UL);
}

uint32_t freqBuzzerAtual = 0;
void tocar(uint32_t freq) {
  // Só reprograma o LEDC quando muda, senão o som fica picotado
  if (freq == freqBuzzerAtual) return;
  ledcWriteTone(CANAL_BUZZER, freq);
  freqBuzzerAtual = freq;
}

void corLed(bool r, bool g, bool b) {
  digitalWrite(PINO_LED_R, r);
  digitalWrite(PINO_LED_G, g);
  digitalWrite(PINO_LED_B, b);
}

void atualizarAtuadores() {
  uint32_t agora = millis();
  bool piscar = (agora / 500) % 2;

  switch (estadoAtual) {
    case NORMAL:  corLed(0, 1, 0); break;
    case ATENCAO: corLed(1, 1, 0); break;
    case ALERTA:  corLed(piscar, 0, 0); break;
    case CRITICO: corLed(1, 0, 0); break;
  }

  // Sirene: bipe curto no ALERTA, tom alternado no CRITICO
  if (sireneSilenciada || estadoAtual < ALERTA) {
    tocar(0);
  } else if (estadoAtual == ALERTA) {
    tocar((agora % 1000) < 200 ? 2000 : 0);
  } else {
    tocar((agora / 400) % 2 ? 1500 : 1000);
  }

  // Cancela: em CRITICO fecha sempre. A decisão é local para não depender da rede
  // justamente no momento em que a chuva costuma derrubar a conexão.
  bool fechar;
  if (estadoAtual == CRITICO) fechar = true;
  else if (modoCancela == CANCELA_FECHAR) fechar = true;
  else if (modoCancela == CANCELA_ABRIR) fechar = false;
  else fechar = false;

  if (fechar != cancelaFechada) {
    cancelaFechada = fechar;
    escreverServo(fechar ? 0 : 90);
    Serial.printf("[ATUADOR] cancela %s\n", fechar ? "FECHADA" : "ABERTA");
  }
}

void linhaLcd(uint8_t linha, const char* texto) {
  char buf[17];
  snprintf(buf, sizeof(buf), "%-16s", texto);
  lcd.setCursor(0, linha);
  lcd.print(buf);
}

void atualizarLcd() {
  char l1[17], l2[17];
  snprintf(l1, sizeof(l1), "%-7s %4.0fcm", nomeEstado(estadoAtual), nivelFiltrado);

  uint32_t fase = (millis() / 3000) % 3;
  if (estadoAtual == CRITICO) {
    snprintf(l2, sizeof(l2), "%s", fase == 2 ? "Use outra rota" : "VIA FECHADA");
  } else if (estadoAtual == ALERTA) {
    snprintf(l2, sizeof(l2), "%s", fase == 2 ? "Agua subindo" : "EVITE A VIA");
  } else if (fase == 0) {
    snprintf(l2, sizeof(l2), "Chuva %3.0f mm/h", chuvaFiltrada);
  } else if (fase == 1) {
    snprintf(l2, sizeof(l2), "Sobe %+5.1fcm/m", taxaSubida);
  } else {
    snprintf(l2, sizeof(l2), "%s Buf:%u", linkOk() ? "Online " : "OFFLINE", (unsigned)buffer.tamanho());
  }
  linhaLcd(0, l1);
  linhaLcd(1, l2);
}

// ---------------- MQTT: publicação ----------------
void publicarEstado() {
  JsonDocument doc;
  bool real;
  uint32_t ts = carimboTempo(real);
  anexarTempo(doc, ts, real);
  doc["estado"] = nomeEstado(estadoAtual);
  doc["codigo"] = (int)estadoAtual;
  doc["motivo"] = nomeMotivo(motivoAtual);
  doc["nivel_cm"] = r1(nivelFiltrado);
  doc["taxa_cm_min"] = r1(taxaSubida);
  doc["chuva_mm_h"] = r1(chuvaFiltrada);
  doc["cancela"] = cancelaFechada ? "fechada" : "aberta";
  doc["sirene"] = (estadoAtual >= ALERTA && !sireneSilenciada) ? "ativa" : "desligada";
  // Retido: quem assinar depois recebe o estado atual na hora
  if (publicarJson(T_ESTADO, doc, true)) estadoPendente = false;
}

bool publicarJanela(const Janela& j) {
  JsonDocument n, c, a;
  anexarTempo(n, j.ts, j.tsReal);
  n["nivel_cm"] = r1(j.nivelMed);
  n["min"] = r1(j.nivelMin);
  n["max"] = r1(j.nivelMax);
  n["taxa_cm_min"] = r1(j.taxa);
  n["estado"] = j.estado;
  n["amostras"] = j.amostras;
  n["buffer"] = j.offline;

  anexarTempo(c, j.ts, j.tsReal);
  c["chuva_mm_h"] = r1(j.chuvaMed);
  c["max"] = r1(j.chuvaMax);
  c["simulado"] = true;
  c["buffer"] = j.offline;

  anexarTempo(a, j.ts, j.tsReal);
  a["temp_c"] = r1(j.temp);
  a["umid_pct"] = r1(j.umid);
  a["buffer"] = j.offline;

  return publicarJson(T_NIVEL, n) && publicarJson(T_CHUVA, c) && publicarJson(T_AMBIENTE, a);
}

void publicarStatusOnline() {
  JsonDocument doc;
  doc["status"] = "online";
  doc["fw"] = FW_VERSAO;
  doc["ip"] = WiFi.localIP().toString();
  doc["rssi"] = WiFi.RSSI();
  doc["buffer_pendente"] = buffer.tamanho();
  publicarJson(T_STATUS, doc, true);
}

void publicarDiagnostico() {
  JsonDocument doc;
  doc["uptime_s"] = millis() / 1000;
  doc["amostras"] = totalAmostras;
  doc["publicadas"] = totalPublicadas;
  doc["evitadas"] = totalEvitadas;
  doc["buffer"] = buffer.tamanho();
  doc["descartadas"] = buffer.descartados();
  doc["reconexoes"] = reconexoesMqtt;
  doc["erros_ultrassom"] = errosUltrassom;
  doc["intervalo_s"] = intervaloJanelaMs / 1000;
  doc["heap"] = ESP.getFreeHeap();
  publicarJson(T_DIAG, doc);
}

// Reenvio do buffer: poucas janelas por volta do loop para não travar a decisão local
void esvaziarBuffer() {
  if (!linkOk() || buffer.vazia()) return;
  for (int i = 0; i < 3 && !buffer.vazia(); i++) {
    Janela j;
    buffer.espiar(j);
    if (!publicarJanela(j)) return;  // falhou: tenta de novo na próxima volta, sem perder
    buffer.remover();
    if (buffer.vazia()) Serial.println("[BUFFER] reenvio concluido");
  }
}

// ---------------- MQTT: comandos remotos ----------------
void responder(const char* acao, bool ok, const char* msg) {
  JsonDocument doc;
  doc["acao"] = acao;
  doc["ok"] = ok;
  doc["msg"] = msg;
  publicarJson(T_ACK, doc);
  Serial.printf("[CMD] %s -> %s (%s)\n", acao, ok ? "ok" : "recusado", msg);
}

void aoReceberMensagem(char* topico, byte* payload, unsigned int tamanho) {
  JsonDocument doc;
  if (deserializeJson(doc, payload, tamanho)) {
    responder("?", false, "JSON invalido");
    return;
  }
  const char* acao = doc["acao"] | "";

  if (strcmp(acao, "silenciar") == 0) {
    sireneSilenciada = true;
    estadoQuandoSilenciou = estadoAtual;
    responder(acao, true, "sirene silenciada ate o estado piorar");

  } else if (strcmp(acao, "cancela") == 0) {
    const char* modo = doc["modo"] | "";
    if (strcmp(modo, "auto") == 0) modoCancela = CANCELA_AUTO;
    else if (strcmp(modo, "fechar") == 0) modoCancela = CANCELA_FECHAR;
    else if (strcmp(modo, "abrir") == 0) {
      // Trava de segurança na borda: ninguém abre a via remotamente com nível crítico
      if (estadoAtual == CRITICO) { responder(acao, false, "nivel critico, abertura bloqueada"); return; }
      modoCancela = CANCELA_ABRIR;
    } else { responder(acao, false, "modo deve ser auto, fechar ou abrir"); return; }
    responder(acao, true, modo);
    estadoPendente = true;

  } else if (strcmp(acao, "limiares") == 0) {
    Limiares novo = limiares;
    novo.atencao = doc["atencao"] | limiares.atencao;
    novo.alerta = doc["alerta"] | limiares.alerta;
    novo.critico = doc["critico"] | limiares.critico;
    novo.histerese = doc["histerese"] | limiares.histerese;
    if (!limiaresValidos(novo, ALTURA_SENSOR_CM)) { responder(acao, false, "limiares fora de ordem ou de faixa"); return; }
    limiares = novo;
    char msg[64];
    snprintf(msg, sizeof(msg), "%.0f/%.0f/%.0f h=%.0f", limiares.atencao, limiares.alerta, limiares.critico, limiares.histerese);
    responder(acao, true, msg);

  } else if (strcmp(acao, "intervalo") == 0) {
    int s = doc["segundos"] | 0;
    if (s < 10 || s > 300) { responder(acao, false, "segundos entre 10 e 300"); return; }
    intervaloJanelaMs = (uint32_t)s * 1000;
    responder(acao, true, "intervalo de agregacao alterado");

  } else if (strcmp(acao, "diagnostico") == 0) {
    publicarDiagnostico();
    responder(acao, true, "diagnostico publicado");

  } else {
    responder(acao, false, "acao desconhecida");
  }
}

// ---------------- Rede: reconexão não bloqueante ----------------
void gerenciarWifi() {
  uint32_t agora = millis();

  if (quedaSimulada) {
    if (WiFi.status() == WL_CONNECTED || wifiTentando) {
      // Derruba sem DISCONNECT do MQTT: o broker só percebe pelo keepalive e
      // publica o Last Will, como numa queda real
      WiFi.disconnect();
      clienteTls.stop();
      wifiTentando = false;
      Serial.println("[REDE] queda simulada pela chave");
    }
    wifiEstavaConectado = false;
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiEstavaConectado) {
      Serial.printf("[REDE] Wi-Fi conectado, IP %s\n", WiFi.localIP().toString().c_str());
      wifiEstavaConectado = true;
      wifiTentando = false;
      if (!ntpConfigurado) {
        configTime(-3 * 3600, 0, "pool.ntp.org", "a.st1.ntp.br");
        ntpConfigurado = true;
      }
    }
    return;
  }

  if (wifiEstavaConectado) {
    Serial.println("[REDE] Wi-Fi perdido");
    wifiEstavaConectado = false;
  }
  // WiFi.begin retorna na hora; o status é conferido nas próximas voltas do loop
  if (!wifiTentando || agora - inicioTentativaWifi > WIFI_TIMEOUT_TENTATIVA_MS) {
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_SENHA, 6);
    wifiTentando = true;
    inicioTentativaWifi = agora;
    Serial.println("[REDE] tentando Wi-Fi...");
  }
}

void gerenciarMqtt() {
  if (quedaSimulada || WiFi.status() != WL_CONNECTED) return;
  if (mqtt.connected()) { mqtt.loop(); return; }

  uint32_t agora = millis();
  if ((int32_t)(agora - proximaTentativaMqtt) < 0) return;

  char clientId[40];
  snprintf(clientId, sizeof(clientId), "%s-%s-%04X", GRUPO, NO_ID, (uint16_t)(ESP.getEfuseMac() & 0xFFFF));
  Serial.printf("[MQTT] conectando em %s:%d...\n", MQTT_HOST, MQTT_PORTA);

  // Uma única tentativa por vez; o intervalo entre tentativas cresce até 30 s
  bool ok = mqtt.connect(clientId, MQTT_USUARIO, MQTT_SENHA,
                         T_STATUS, 1, true, "{\"status\":\"offline\"}", true);
  if (ok) {
    reconexoesMqtt++;
    backoffMqtt = MQTT_BACKOFF_INICIAL_MS;
    mqtt.subscribe(T_CMD, 1);
    publicarStatusOnline();
    estadoPendente = true;
    Serial.printf("[MQTT] conectado. Buffer pendente: %u\n", (unsigned)buffer.tamanho());
  } else {
    Serial.printf("[MQTT] falhou (rc=%d), nova tentativa em %lu ms\n", mqtt.state(), (unsigned long)backoffMqtt);
    proximaTentativaMqtt = agora + backoffMqtt;
    backoffMqtt = min(backoffMqtt * 2, MQTT_BACKOFF_MAXIMO_MS);
  }
}

// ---------------- Dashboard ----------------
void enviarThingSpeak() {
  if (quedaSimulada || WiFi.status() != WL_CONNECTED) return;
  char url[320];
  snprintf(url, sizeof(url),
           "http://api.thingspeak.com/update?api_key=%s&field1=%.1f&field2=%.1f&field3=%.1f"
           "&field4=%.1f&field5=%d&field6=%.1f&field7=%lu&field8=%u",
           THINGSPEAK_WRITE_KEY, nivelFiltrado, chuvaFiltrada, temperatura, umidade,
           (int)estadoAtual, taxaSubida, (unsigned long)totalEvitadas, (unsigned)buffer.tamanho());
  WiFiClient cliente;
  HTTPClient http;
  http.setTimeout(2000);
  http.begin(cliente, url);
  int codigo = http.GET();
  http.end();
  Serial.printf("[THINGSPEAK] HTTP %d\n", codigo);
}

// ---------------- Sensores ----------------
void lerUltrassom() {
  digitalWrite(PINO_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PINO_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PINO_TRIG, LOW);
  uint32_t eco = pulseIn(PINO_ECHO, HIGH, 30000);

  float dist = distanciaCm(eco, temperatura);
  if (eco == 0 || dist < 2.0f || dist > 400.0f) {
    errosUltrassom++;  // leitura fora da faixa do HC-SR04 é descartada
    return;
  }
  float nivel = constrain(ALTURA_SENSOR_CM - dist, 0.0f, ALTURA_SENSOR_CM);
  filtroNivel.adicionar(nivel);
  nivelFiltrado = filtroNivel.media();
  aggNivel.adicionar(nivelFiltrado);
  amostrasBrutasJanela++;
  totalAmostras++;
}

void lerChuva() {
  float chuva = analogRead(PINO_CHUVA) * CHUVA_MAX_MM_H / 4095.0f;
  filtroChuva.adicionar(chuva);
  chuvaFiltrada = filtroChuva.media();
  aggChuva.adicionar(chuvaFiltrada);
  amostrasBrutasJanela++;
  totalAmostras++;
}

void lerDht() {
  TempAndHumidity th = dht.getTempAndHumidity();
  if (isnan(th.temperature) || isnan(th.humidity)) return;  // mantém a última leitura boa
  temperatura = th.temperature;
  umidade = th.humidity;
  aggTemp.adicionar(temperatura);
  aggUmid.adicionar(umidade);
  amostrasBrutasJanela++;
  totalAmostras++;
}

void lerBotao() {
  static bool anterior = HIGH;
  static uint32_t tMudanca = 0;
  bool leitura = digitalRead(PINO_BOTAO);
  if (leitura != anterior && millis() - tMudanca > 50) {
    tMudanca = millis();
    anterior = leitura;
    if (leitura == LOW && estadoAtual >= ALERTA) {
      sireneSilenciada = true;
      estadoQuandoSilenciou = estadoAtual;
      Serial.println("[LOCAL] sirene silenciada pelo botao");
      estadoPendente = true;
    }
  }
}

// ---------------- Decisão na borda ----------------
void atualizarDecisao() {
  if (filtroNivel.quantidade() < 3) return;  // espera o filtro ter dados mínimos

  estadoNivel = classificarNivel(nivelFiltrado, estadoNivel, limiares);
  Decisao d = decidir(estadoNivel, taxaSubida, chuvaFiltrada, antecipacao);

  if (d.estado != estadoAtual) {
    Serial.printf("[BORDA] %s -> %s (motivo: %s, nivel %.1f cm, taxa %.1f cm/min, chuva %.0f mm/h)\n",
                  nomeEstado(estadoAtual), nomeEstado(d.estado), nomeMotivo(d.motivo),
                  nivelFiltrado, taxaSubida, chuvaFiltrada);
    // Se piorou depois de silenciar, a sirene volta a tocar
    if (d.estado > estadoQuandoSilenciou) sireneSilenciada = false;
    if (d.estado < ALERTA) sireneSilenciada = false;
    // Abertura manual não se mantém se o nível ficar crítico
    if (d.estado == CRITICO && modoCancela == CANCELA_ABRIR) modoCancela = CANCELA_AUTO;
    estadoAtual = d.estado;
    motivoAtual = d.motivo;
    estadoPendente = true;
  }
}

// Fecha a janela de agregação e decide se publica (deadband) ou guarda no buffer
void fecharJanela() {
  uint32_t agora = millis();
  if (aggNivel.amostras() == 0) return;

  Janela j;
  j.ts = carimboTempo(j.tsReal);
  j.offline = !linkOk();
  j.nivelMed = aggNivel.media();
  j.nivelMin = aggNivel.minimo();
  j.nivelMax = aggNivel.maximo();
  j.taxa = taxaSubida;
  j.chuvaMed = aggChuva.media();
  j.chuvaMax = aggChuva.maximo();
  j.temp = aggTemp.amostras() ? aggTemp.media() : temperatura;
  j.umid = aggUmid.amostras() ? aggUmid.media() : umidade;
  j.estado = estadoAtual;
  j.amostras = aggNivel.amostras();

  bool publicar = devePublicarTelemetria(j.nivelMed, ultimoNivelPublicado, jaPublicouTelemetria,
                                         estadoAtual, agora - instanteUltimaTelemetria,
                                         BANDA_MORTA_CM, HEARTBEAT_TELEMETRIA_MS);
  // Envio ingênuo mandaria cada amostra bruta como mensagem; a borda manda 3 por janela, ou nenhuma
  uint32_t enviadas = publicar ? 3 : 0;
  if (amostrasBrutasJanela > enviadas) totalEvitadas += amostrasBrutasJanela - enviadas;

  if (publicar) {
    ultimoNivelPublicado = j.nivelMed;
    jaPublicouTelemetria = true;
    instanteUltimaTelemetria = agora;
    // Tudo passa pela fila: online ela esvazia na mesma volta, offline acumula
    if (!buffer.inserir(j)) Serial.println("[BUFFER] cheio, janela mais antiga descartada");
    if (j.offline) Serial.printf("[BUFFER] sem link, guardado (%u/%u)\n", (unsigned)buffer.tamanho(), (unsigned)buffer.capacidade());
  }

  aggNivel.reiniciar(); aggChuva.reiniciar(); aggTemp.reiniciar(); aggUmid.reiniciar();
  amostrasBrutasJanela = 0;
}

// ---------------- Setup / loop ----------------
void setup() {
  Serial.begin(115200);
  pinMode(PINO_TRIG, OUTPUT);
  pinMode(PINO_ECHO, INPUT);
  pinMode(PINO_BOTAO, INPUT_PULLUP);
  pinMode(PINO_QUEDA, INPUT);  // chave ligada entre GND e 3V3, nível sempre definido
  posicaoChaveBoot = digitalRead(PINO_QUEDA);
  pinMode(PINO_LED_R, OUTPUT);
  pinMode(PINO_LED_G, OUTPUT);
  pinMode(PINO_LED_B, OUTPUT);
  analogReadResolution(12);

  ledcSetup(CANAL_SERVO, 50, 16);
  ledcAttachPin(PINO_SERVO, CANAL_SERVO);
  escreverServo(90);
  ledcSetup(CANAL_BUZZER, 2000, 10);
  ledcAttachPin(PINO_BUZZER, CANAL_BUZZER);
  ledcWriteTone(CANAL_BUZZER, 0);

  dht.setup(PINO_DHT, DHTesp::DHT22);
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  linhaLcd(0, "Sentinela");
  linhaLcd(1, "Pirajucara " NO_ID);
  corLed(0, 0, 1);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);  // a reconexão é feita por gerenciarWifi()
  clienteTls.setCACert(CA_RAIZ_HIVEMQ);
  clienteTls.setHandshakeTimeout(8);
  mqtt.setServer(MQTT_HOST, MQTT_PORTA);
  mqtt.setCallback(aoReceberMensagem);
  mqtt.setBufferSize(512);
  mqtt.setKeepAlive(MQTT_KEEPALIVE_S);
  mqtt.setSocketTimeout(5);

  Serial.printf("[BOOT] Sentinela Pirajucara %s fw %s\n", NO_ID, FW_VERSAO);
  Serial.println("[BOOT] base de topicos: " BASE);
  tJanela = millis();
  delay(1500);  // tempo da tela inicial e da estabilização do DHT22
}

void loop() {
  uint32_t agora = millis();

  quedaSimulada = digitalRead(PINO_QUEDA) != posicaoChaveBoot;
  gerenciarWifi();
  gerenciarMqtt();

  if (agora - tUltrassom >= PERIODO_ULTRASSOM_MS) { tUltrassom = agora; lerUltrassom(); }
  if (agora - tChuva >= PERIODO_CHUVA_MS) { tChuva = agora; lerChuva(); }
  if (agora - tDht >= PERIODO_DHT_MS) { tDht = agora; lerDht(); }
  if (agora - tTaxa >= PERIODO_TAXA_MS) {
    tTaxa = agora;
    historicoTaxa.registrar(nivelFiltrado, agora);
    taxaSubida = historicoTaxa.taxaCmPorMin();
  }
  lerBotao();

  // A decisão e os atuadores rodam a cada volta, com ou sem rede
  atualizarDecisao();
  atualizarAtuadores();

  if (agora - tJanela >= intervaloJanelaMs) { tJanela = agora; fecharJanela(); }
  if (estadoPendente && linkOk()) publicarEstado();
  esvaziarBuffer();

  if (agora - tLcd >= PERIODO_LCD_MS) { tLcd = agora; atualizarLcd(); }
  if (agora - tThingSpeak >= PERIODO_THINGSPEAK_MS) { tThingSpeak = agora; enviarThingSpeak(); }
  if (agora - tDiag >= PERIODO_DIAGNOSTICO_MS && linkOk()) { tDiag = agora; publicarDiagnostico(); }
}
