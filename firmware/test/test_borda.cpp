// Testes da lógica de borda (include/borda.h) rodando no PC, sem ESP32.
// Compilar: c++ -std=c++17 -I../include test_borda.cpp -o test_borda
#include <cmath>
#include <cstdio>

#include "borda.h"

using namespace borda;

static int falhas = 0, total = 0;

#define CONFERE(cond)                                                     \
  do {                                                                    \
    total++;                                                              \
    if (!(cond)) { falhas++; printf("  FALHOU linha %d: %s\n", __LINE__, #cond); } \
  } while (0)

static bool perto(float a, float b, float tol = 0.01f) { return std::fabs(a - b) <= tol; }

void testeMediaMovel() {
  printf("MediaMovel\n");
  MediaMovel<4> m;
  m.adicionar(10);
  CONFERE(perto(m.media(), 10));
  m.adicionar(20);
  CONFERE(perto(m.media(), 15));
  m.adicionar(30); m.adicionar(40); m.adicionar(50);  // 10 sai da janela
  CONFERE(perto(m.media(), 35));
  CONFERE(m.quantidade() == 4);
}

void testeAgregador() {
  printf("Agregador\n");
  Agregador a;
  a.adicionar(5); a.adicionar(-2); a.adicionar(9);
  CONFERE(perto(a.minimo(), -2));
  CONFERE(perto(a.maximo(), 9));
  CONFERE(perto(a.media(), 4));
  a.reiniciar();
  CONFERE(a.amostras() == 0);
}

void testeFilaCircular() {
  printf("FilaCircular\n");
  FilaCircular<int, 3> f;
  CONFERE(f.vazia());
  CONFERE(f.inserir(1) && f.inserir(2) && f.inserir(3));
  CONFERE(!f.inserir(4));          // cheia: descarta o 1
  CONFERE(f.descartados() == 1);
  int v = 0;
  CONFERE(f.espiar(v) && v == 2);  // mais antigo agora é o 2
  CONFERE(f.remover() && f.espiar(v) && v == 3);
  CONFERE(f.remover() && f.espiar(v) && v == 4);
  CONFERE(f.remover() && f.vazia() && !f.remover());
}

void testeHisterese() {
  printf("Histerese\n");
  Limiares l;  // 150 / 220 / 280, histerese 10
  Estado e = NORMAL;
  e = classificarNivel(149, e, l); CONFERE(e == NORMAL);
  e = classificarNivel(150, e, l); CONFERE(e == ATENCAO);
  e = classificarNivel(145, e, l); CONFERE(e == ATENCAO);  // dentro da histerese
  e = classificarNivel(139, e, l); CONFERE(e == NORMAL);
  e = classificarNivel(285, e, l); CONFERE(e == CRITICO);  // salto direto
  e = classificarNivel(272, e, l); CONFERE(e == CRITICO);
  e = classificarNivel(200, e, l); CONFERE(e == ATENCAO);  // desce dois degraus de uma vez
  e = classificarNivel(10, e, l);  CONFERE(e == NORMAL);
}

void testeOscilacaoNoLimiar() {
  printf("Oscilacao perto do limiar\n");
  // Nível oscilando 215..225 em volta do limiar de alerta (220)
  Limiares l;
  Estado e = ATENCAO;
  int trocas = 0;
  for (int i = 0; i < 40; i++) {
    float nivel = 220 + 5 * std::sin(i * 0.8f);
    Estado novo = classificarNivel(nivel, e, l);
    if (novo != e) trocas++;
    e = novo;
  }
  CONFERE(trocas == 1);  // sobe uma vez e fica em ALERTA
  CONFERE(e == ALERTA);
}

void testeAntecipacao() {
  printf("Antecipacao\n");
  Antecipacao a;
  Decisao d = decidir(ATENCAO, 1.0f, 10.0f, a);
  CONFERE(d.estado == ATENCAO && d.motivo == MOTIVO_NIVEL);
  d = decidir(ATENCAO, 5.0f, 10.0f, a);
  CONFERE(d.estado == ALERTA && d.motivo == MOTIVO_TAXA);
  d = decidir(ATENCAO, 0.0f, 60.0f, a);
  CONFERE(d.estado == ALERTA && d.motivo == MOTIVO_CHUVA);
  d = decidir(NORMAL, 5.0f, 60.0f, a);
  CONFERE(d.estado == ATENCAO);
  d = decidir(NORMAL, 0.0f, 80.0f, a);  // chuva sem subida não muda nada
  CONFERE(d.estado == NORMAL);
  d = decidir(CRITICO, 0.0f, 0.0f, a);
  CONFERE(d.estado == CRITICO);
}

void testeLimiaresValidos() {
  printf("Validacao de limiares\n");
  Limiares l;
  CONFERE(limiaresValidos(l, 350));
  l.alerta = 140;  // abaixo da atenção
  CONFERE(!limiaresValidos(l, 350));
  l = Limiares(); l.critico = 360;  // acima do sensor
  CONFERE(!limiaresValidos(l, 350));
  l = Limiares(); l.histerese = 80;  // maior que a faixa entre limiares
  CONFERE(!limiaresValidos(l, 350));
}

void testeDistancia() {
  printf("Distancia com compensacao de temperatura\n");
  // 5830 us a 20 C: v = 343,42 m/s -> 100,1 cm
  CONFERE(perto(distanciaCm(5830, 20.0f), 100.1f, 0.1f));
  // A 35 C o mesmo eco representa uma distância maior
  CONFERE(distanciaCm(5830, 35.0f) > distanciaCm(5830, 20.0f));
}

void testeTaxa() {
  printf("Taxa de subida\n");
  HistoricoTaxa<13> h;
  CONFERE(perto(h.taxaCmPorMin(), 0));
  for (int i = 0; i < 13; i++) h.registrar(100 + i * 0.5f, i * 5000);  // 0,5 cm a cada 5 s
  CONFERE(perto(h.taxaCmPorMin(), 6.0f));
  for (int i = 13; i < 20; i++) h.registrar(106, i * 5000);  // estabilizou
  CONFERE(h.taxaCmPorMin() < 6.0f);
}

void testeDeadband() {
  printf("Deadband\n");
  CONFERE(devePublicarTelemetria(50, 0, false, NORMAL, 0, 2, 300000));      // primeira
  CONFERE(!devePublicarTelemetria(51, 50, true, NORMAL, 30000, 2, 300000)); // variou 1 cm
  CONFERE(devePublicarTelemetria(52.5f, 50, true, NORMAL, 30000, 2, 300000));
  CONFERE(devePublicarTelemetria(50, 50, true, NORMAL, 300000, 2, 300000)); // heartbeat
  CONFERE(devePublicarTelemetria(50, 50, true, ALERTA, 30000, 2, 300000));  // fora do normal sempre
}

// Cenário de enchente simulado: 2 h de leitura a 2 Hz. Mede quando a borda alerta
// e quantas mensagens deixa de enviar em comparação com o envio de cada amostra.
void testeCenarioEnchente() {
  printf("Cenario de enchente (2 h)\n");
  Limiares l;
  Antecipacao a;
  MediaMovel<8> filtro;
  HistoricoTaxa<13> hist;
  Estado porNivel = NORMAL, final = NORMAL;
  float ultimoPub = 0; bool jaPub = false; uint32_t tUltPub = 0;
  uint32_t brutas = 0, publicadas = 0, eventos = 0;
  float taxa = 0;
  int tAntecipado = -1, tPorNivel = -1;

  for (uint32_t t = 0; t < 7200000; t += 500) {
    float min = t / 60000.0f;
    // 0-40 min estável em 60 cm; 40-80 min sobe 5 cm/min; depois desce 3 cm/min
    float nivel = 60;
    if (min > 40) nivel = 60 + (min - 40) * 5.0f;
    if (min > 80) nivel = 260 - (min - 80) * 3.0f;
    float ruido = 1.5f * std::sin(t * 0.0137f);
    float chuva = (min > 35 && min < 75) ? 55.0f : 5.0f;

    filtro.adicionar(nivel + ruido);
    float filtrado = filtro.media();
    brutas += 2;                       // nível + chuva
    if (t % 2500 == 0) brutas++;       // DHT22
    if (t % 5000 == 0) { hist.registrar(filtrado, t); taxa = hist.taxaCmPorMin(); }

    porNivel = classificarNivel(filtrado, porNivel, l);
    Decisao d = decidir(porNivel, taxa, chuva, a);
    if (d.estado != final) { eventos++; publicadas++; final = d.estado; }
    if (final >= ALERTA && tAntecipado < 0) tAntecipado = (int)(t / 1000);
    if (porNivel >= ALERTA && tPorNivel < 0) tPorNivel = (int)(t / 1000);

    if (t % 30000 == 0 && t > 0) {
      if (devePublicarTelemetria(filtrado, ultimoPub, jaPub, final, t - tUltPub, 2, 300000)) {
        publicadas += 3; ultimoPub = filtrado; jaPub = true; tUltPub = t;
      }
    }
  }
  float reducao = 100.0f * (brutas - publicadas) / brutas;
  printf("  amostras brutas: %u | mensagens publicadas: %u | eventos de estado: %u\n",
         brutas, publicadas, eventos);
  printf("  reducao de trafego: %.2f%%\n", reducao);
  printf("  ALERTA por nivel em t=%d s; com antecipacao em t=%d s (%d s antes)\n",
         tPorNivel, tAntecipado, tPorNivel - tAntecipado);
  CONFERE(tAntecipado >= 0 && tAntecipado < tPorNivel);
  CONFERE(reducao > 95.0f);
}

int main() {
  testeMediaMovel();
  testeAgregador();
  testeFilaCircular();
  testeHisterese();
  testeOscilacaoNoLimiar();
  testeAntecipacao();
  testeLimiaresValidos();
  testeDistancia();
  testeTaxa();
  testeDeadband();
  testeCenarioEnchente();
  printf("\n%d verificacoes, %d falhas\n", total, falhas);
  return falhas ? 1 : 0;
}
