// Lógica de processamento na borda, sem dependência do Arduino.
// Fica separada do main.cpp para poder ser testada no PC (test/test_borda.cpp).
#pragma once

#include <stdint.h>
#include <stddef.h>

namespace borda {

// Estados do nó, em ordem crescente de gravidade (o valor vai para o dashboard).
enum Estado : uint8_t { NORMAL = 0, ATENCAO = 1, ALERTA = 2, CRITICO = 3 };

inline const char* nomeEstado(Estado e) {
  switch (e) {
    case NORMAL:  return "NORMAL";
    case ATENCAO: return "ATENCAO";
    case ALERTA:  return "ALERTA";
    default:      return "CRITICO";
  }
}

// Média móvel de N amostras. Antes de encher, a média usa só as amostras que já chegaram.
template <size_t N>
class MediaMovel {
 public:
  void adicionar(float valor) {
    soma_ -= janela_[pos_];
    janela_[pos_] = valor;
    soma_ += valor;
    pos_ = (pos_ + 1) % N;
    if (qtd_ < N) qtd_++;
  }
  float media() const { return qtd_ ? soma_ / qtd_ : 0.0f; }
  size_t quantidade() const { return qtd_; }
  void limpar() {
    for (size_t i = 0; i < N; i++) janela_[i] = 0;
    soma_ = 0; pos_ = 0; qtd_ = 0;
  }

 private:
  float janela_[N] = {0};
  float soma_ = 0;
  size_t pos_ = 0;
  size_t qtd_ = 0;
};

// Acumula mínimo, máximo e média de uma grandeza dentro da janela de publicação.
class Agregador {
 public:
  void adicionar(float v) {
    if (n_ == 0 || v < min_) min_ = v;
    if (n_ == 0 || v > max_) max_ = v;
    soma_ += v;
    n_++;
  }
  float media() const { return n_ ? soma_ / n_ : 0.0f; }
  float minimo() const { return min_; }
  float maximo() const { return max_; }
  uint32_t amostras() const { return n_; }
  void reiniciar() { n_ = 0; soma_ = 0; min_ = 0; max_ = 0; }

 private:
  uint32_t n_ = 0;
  float soma_ = 0, min_ = 0, max_ = 0;
};

// Fila circular de tamanho fixo. Quando cheia, a leitura mais antiga é sobrescrita:
// numa enchente o dado recente vale mais que o antigo.
template <typename T, size_t N>
class FilaCircular {
 public:
  // Retorna false quando precisou descartar a leitura mais antiga.
  bool inserir(const T& item) {
    bool semPerda = true;
    if (qtd_ == N) {
      inicio_ = (inicio_ + 1) % N;
      qtd_--;
      descartados_++;
      semPerda = false;
    }
    itens_[(inicio_ + qtd_) % N] = item;
    qtd_++;
    return semPerda;
  }
  bool espiar(T& saida) const {
    if (qtd_ == 0) return false;
    saida = itens_[inicio_];
    return true;
  }
  bool remover() {
    if (qtd_ == 0) return false;
    inicio_ = (inicio_ + 1) % N;
    qtd_--;
    return true;
  }
  size_t tamanho() const { return qtd_; }
  size_t capacidade() const { return N; }
  bool vazia() const { return qtd_ == 0; }
  uint32_t descartados() const { return descartados_; }

 private:
  T itens_[N];
  size_t inicio_ = 0;
  size_t qtd_ = 0;
  uint32_t descartados_ = 0;
};

// Limiares de nível em cm medidos a partir do leito do córrego.
struct Limiares {
  float atencao = 150.0f;
  float alerta = 220.0f;
  float critico = 280.0f;
  float histerese = 10.0f;
};

inline bool limiaresValidos(const Limiares& l, float alturaMaxima) {
  return l.atencao > 0 && l.atencao < l.alerta && l.alerta < l.critico &&
         l.critico < alturaMaxima && l.histerese >= 0 &&
         l.histerese < (l.alerta - l.atencao) && l.histerese < (l.critico - l.alerta);
}

// Classifica só pelo nível, com histerese: para subir basta cruzar o limiar,
// para descer o nível precisa ficar abaixo de (limiar - histerese).
// Isso evita a sirene ligando e desligando quando a água oscila perto do limiar.
inline Estado classificarNivel(float nivel, Estado atual, const Limiares& l) {
  const float lim[4] = {0.0f, l.atencao, l.alerta, l.critico};

  Estado porSubida = NORMAL;
  for (int k = 3; k >= 1; k--) {
    if (nivel >= lim[k]) { porSubida = static_cast<Estado>(k); break; }
  }
  if (porSubida >= atual) return porSubida;

  // Nível abaixo do limiar do estado atual: desce um degrau por vez enquanto
  // estiver abaixo da faixa de histerese.
  int e = atual;
  while (e > 0 && nivel < lim[e] - l.histerese) e--;
  return static_cast<Estado>(e);
}

// Motivo da última classificação, publicado junto com o evento de estado.
enum Motivo : uint8_t { MOTIVO_NIVEL = 0, MOTIVO_TAXA = 1, MOTIVO_CHUVA = 2 };

inline const char* nomeMotivo(Motivo m) {
  switch (m) {
    case MOTIVO_TAXA:  return "taxa_subida";
    case MOTIVO_CHUVA: return "chuva_forte";
    default:           return "nivel";
  }
}

struct Antecipacao {
  float taxaAlerta = 4.0f;    // cm/min de subida que antecipa o alerta
  float chuvaForte = 50.0f;   // mm/h, faixa de chuva muito forte
};

struct Decisao {
  Estado estado;
  Motivo motivo;
};

// Antecipa o alerta quando a água sobe rápido ou a chuva é forte.
// O nível sozinho demora a reagir; a taxa de subida dá alguns minutos de vantagem.
inline Decisao decidir(Estado porNivel, float taxaCmMin, float chuvaMmH, const Antecipacao& a) {
  Decisao d{porNivel, MOTIVO_NIVEL};
  if (porNivel == ATENCAO) {
    if (taxaCmMin >= a.taxaAlerta)      d = {ALERTA, MOTIVO_TAXA};
    else if (chuvaMmH >= a.chuvaForte)  d = {ALERTA, MOTIVO_CHUVA};
  } else if (porNivel == NORMAL && taxaCmMin >= a.taxaAlerta && chuvaMmH >= a.chuvaForte) {
    d = {ATENCAO, MOTIVO_TAXA};
  }
  return d;
}

// Distância do HC-SR04 corrigida pela temperatura do DHT22.
// v(T) = 331,3 + 0,606*T m/s; o pulso de eco cobre ida e volta.
inline float distanciaCm(uint32_t duracaoEcoUs, float temperaturaC) {
  float vCmPorUs = (331.3f + 0.606f * temperaturaC) / 10000.0f;
  return duracaoEcoUs * vCmPorUs / 2.0f;
}

// Calcula a taxa de subida (cm/min) a partir de pontos do nível filtrado
// guardados a cada intervalo fixo.
template <size_t N>
class HistoricoTaxa {
 public:
  void registrar(float nivel, uint32_t instanteMs) {
    niveis_[pos_] = nivel;
    instantes_[pos_] = instanteMs;
    pos_ = (pos_ + 1) % N;
    if (qtd_ < N) qtd_++;
  }
  float taxaCmPorMin() const {
    if (qtd_ < 2) return 0.0f;
    size_t ultimo = (pos_ + N - 1) % N;
    size_t primeiro = (qtd_ < N) ? 0 : pos_;
    uint32_t dt = instantes_[ultimo] - instantes_[primeiro];
    if (dt == 0) return 0.0f;
    return (niveis_[ultimo] - niveis_[primeiro]) * 60000.0f / dt;
  }

 private:
  float niveis_[N] = {0};
  uint32_t instantes_[N] = {0};
  size_t pos_ = 0;
  size_t qtd_ = 0;
};

// Deadband da telemetria: em estado NORMAL só publica se o nível mudou mais que
// a banda morta ou se passou o tempo máximo sem enviar (heartbeat).
inline bool devePublicarTelemetria(float nivelAtual, float ultimoPublicado, bool jaPublicou,
                                   Estado estado, uint32_t msDesdeUltima,
                                   float bandaCm, uint32_t heartbeatMs) {
  if (!jaPublicou) return true;
  if (estado != NORMAL) return true;
  float delta = nivelAtual - ultimoPublicado;
  if (delta < 0) delta = -delta;
  if (delta >= bandaCm) return true;
  return msDesdeUltima >= heartbeatMs;
}

}  // namespace borda
