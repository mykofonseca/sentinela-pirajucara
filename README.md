# Sentinela Pirajuçara

Nó de borda com ESP32 que monitora o nível do córrego Pirajuçara sob uma ponte da região da Av. Eliseu de Almeida (São Paulo), decide localmente o risco de alagamento, avisa quem está passando (LCD, LED RGB e sirene) e fecha a cancela da ponte quando o nível fica crítico, mesmo sem rede. Os dados agregados e os eventos seguem para o broker público da HiveMQ (MQTT com TLS) e para um dashboard no ThingSpeak.

Projeto da Global Solution 2026.2 (CitizenTech) da disciplina de Edge Computing, Ciência da Computação, FIAP.

## Links

- Vídeo de demonstração (YouTube, não listado): https://youtu.be/ENHdPyjKutI
- Dashboard público no ThingSpeak: https://thingspeak.mathworks.com/channels/3586730
- Relatório técnico: [docs/Relatorio_GS_EdgeComputing_SentinelaPirajucara.pdf](docs/Relatorio_GS_EdgeComputing_SentinelaPirajucara.pdf)

## Integrantes

| Nome | RM | E-mail |
|---|---|---|
| Gustavo Franzoti Gonçalves | RM566983 | RM566983@fiap.com.br |
| Maykon Santana Fonseca | RM567041 | RM567041@fiap.com.br |

## Tecnologias

- ESP32 DevKit C v4 simulado no VS Code com as extensões PlatformIO IDE e Wokwi Simulator, firmware em C++ (Arduino)
- Sensores: HC-SR04 (nível), potenciômetro simulando pluviômetro, DHT22, botão
- Atuadores: LCD 16x2 I2C, LED RGB, buzzer, servo (cancela)
- MQTT: broker público da HiveMQ (`broker.hivemq.com`, porta 8883 com TLS), bibliotecas PubSubClient e ArduinoJson 7, acompanhamento pelo [MQTT WebSocket Client](https://www.hivemq.com/demos/websocket-client/)
- Dashboard: ThingSpeak (REST/HTTP)
- Diagramas em Mermaid; relatório gerado com python-docx e exportado pelo Word

## Estrutura de pastas

```
firmware/                 projeto PlatformIO
  src/main.cpp            sensores, atuadores, MQTT, reconexão, buffer, ThingSpeak
  include/borda.h         lógica de borda (filtro, histerese, antecipação, fila circular)
  include/secrets.exemplo.h  modelo de credenciais (o secrets.h real fica fora do git)
  test/test_borda.cpp     testes da lógica de borda que rodam no PC
  diagram.json            circuito do Wokwi
  wokwi.toml              liga o Wokwi ao firmware compilado
  .vscode/extensions.json extensões recomendadas (PlatformIO e Wokwi)
docs/
  diagramas/*.mmd         fontes Mermaid
  img/*.png               diagramas exportados
  prints/                 capturas de tela usadas no relatório
  links.json              links do vídeo e do repositório
  gerar_relatorio.py      gera o DOCX e o PDF do relatório
  Relatorio_GS_EdgeComputing_SentinelaPirajucara.pdf
scripts/empacotar.py      gera o .zip do PlatformIO para a entrega
```

## Como executar

### 1. Credenciais

1. O plano gratuito HiveMQ Cloud Serverless foi descontinuado (não aceita novos clusters e os existentes são apagados em 31/12/2026). Por isso o projeto usa o broker público `broker.hivemq.com`, que não precisa de conta. Os comandos são protegidos por um token definido no `secrets.h` (`TOKEN_COMANDO`).
2. No ThingSpeak, crie um canal com os campos 1 a 8 (nível, chuva, temperatura, umidade, estado, taxa de subida, mensagens evitadas, buffer) e copie a *Write API Key*.
3. Copie o modelo e preencha `TOKEN_COMANDO` (um valor só do grupo) e `THINGSPEAK_WRITE_KEY`:

```bash
cp firmware/include/secrets.exemplo.h firmware/include/secrets.h
```

### 2. Preparar o VS Code

1. Instale as extensões **PlatformIO IDE** e **Wokwi Simulator** (o VS Code sugere as duas ao abrir a pasta, por causa do `.vscode/extensions.json`).
2. Abra a pasta **`firmware`** em *File → Open Folder*. Ela precisa ser a raiz do workspace, porque é ali que ficam o `platformio.ini` e o `wokwi.toml`.
3. Ative a licença gratuita do Wokwi: `F1` → **Wokwi: Request a New License** e confirme no navegador.

### 3. Compilar e simular

```bash
pio run
```

(ou o botão ✓ *Build* na barra do PlatformIO). Depois, `F1` → **Wokwi: Start Simulator**. O `wokwi.toml` aponta para `.pio/build/esp32dev/firmware.bin`, então toda alteração no código precisa de um novo build antes de reiniciar a simulação. O monitor serial aparece no painel inferior do VS Code e o circuito no `diagram.json`.

> **Atenção:** o Wokwi pausa a simulação quando a aba dele fica escondida. Se você trocar para o navegador, o ESP32 congela, o broker publica `{"status":"offline"}` (LWT) e os comandos ficam sem `ack`. Deixe o VS Code e o navegador lado a lado.

### 4. Testar no simulador

- Com a simulação rodando, clique no HC-SR04 (o sensor azul com dois "olhos") e arraste a distância para baixo. O nível é `350 − distância`. Ele começa em 400 cm, que dá nível 0 (NORMAL).
  - 200 cm → nível 150 cm (ATENCAO)
  - 130 cm → nível 220 cm (ALERTA)
  - 50 cm → nível 300 cm (CRITICO, LCD mostra "VIA FECHADA" e a cancela fecha). O limite é 280 cm; com 70 cm a leitura fica em ~279,7 cm e o nó permanece em ALERTA.
  - O firmware faz média antes de decidir, então espere alguns segundos em cada posição.
- Gire o potenciômetro acima da metade (> 50 mm/h) com o nó em ATENCAO para ver o alerta antecipado por chuva.
- Aperte o botão azul para silenciar a sirene.
- Mude a chave deslizante (ao lado do botão azul, pino 27) de posição para simular queda de rede. O LCD mostra `OFFLINE Buf:N`; como cada janela dura 30 s, deixe pelo menos 1 minuto para acumular. Volte a chave: o serial mostra `[BUFFER] reenvio concluido` e as mensagens chegam no broker com `"buffer":true`.
- Abra o [MQTT WebSocket Client](https://www.hivemq.com/demos/websocket-client/) com host `broker.hivemq.com`, porta `8884` e **SSL** marcado, e clique em **Connect**.
- Em **Subscriptions**, assine `citizentech/gs-gm/pirajucara-eliseu/no01/#`.
- Em **Publish**, use o tópico `citizentech/gs-gm/pirajucara-eliseu/no01/cmd` e mande os comandos com o token do `secrets.h`:

```json
{"acao":"cancela","modo":"fechar","token":"SEU_TOKEN"}
{"acao":"silenciar","token":"SEU_TOKEN"}
{"acao":"limiares","atencao":120,"alerta":200,"critico":260,"token":"SEU_TOKEN"}
{"acao":"intervalo","segundos":15,"token":"SEU_TOKEN"}
{"acao":"diagnostico","token":"SEU_TOKEN"}
```

Um comando aceito volta em `.../ack` como `{"acao":"cancela","ok":true,"msg":"fechar"}`. Um comando sem token, ou com token errado, volta como `{"ok":false,"msg":"token invalido"}`. Com o nó em CRITICO, `"modo":"abrir"` é recusado com `nivel critico, abertura bloqueada` (trava de segurança na borda); `"modo":"auto"` devolve a cancela ao controle automático.

No ThingSpeak o envio é a cada 20 s (o plano gratuito aceita 1 a cada 15 s), então espere 1 a 2 minutos e atualize a página do canal.

### 5. Testes da lógica de borda no PC

Com qualquer compilador C++17 (g++, clang ou `python -m ziglang c++` após `pip install ziglang`):

```bash
cd firmware/test
g++ -std=c++17 -I../include test_borda.cpp -o test_borda
./test_borda
```

Saída obtida:

```
Cenario de enchente (2 h)
  amostras brutas: 31680 | mensagens publicadas: 505 | eventos de estado: 4
  reducao de trafego: 98.41%
  ALERTA por nivel em t=4320 s; com antecipacao em t=3478 s (842 s antes)

48 verificacoes, 0 falhas
```

### 6. Relatório e pacote de entrega

```bash
pip install python-docx pygments pillow pywin32
python docs/gerar_relatorio.py      # precisa do Microsoft Word para atualizar o sumário e exportar o PDF
python scripts/empacotar.py         # gera entrega/...PlatformIO.zip com o secrets.h preenchido
```

## Tópicos MQTT

Base: `citizentech/gs-gm/pirajucara-eliseu/no01/`

| Tópico | Sentido | Conteúdo |
|---|---|---|
| `nivel` | pub | média, mínimo, máximo, taxa de subida e estado da janela de 30 s |
| `chuva` | pub | intensidade média e máxima (simulada) |
| `ambiente` | pub | temperatura e umidade |
| `estado` | pub, retido | evento de mudança de estado com motivo, cancela e sirene |
| `status` | pub, retido, LWT | `online` / `offline` |
| `diagnostico` | pub | contadores de amostras, mensagens evitadas, buffer e reconexões |
| `ack` | pub | resposta aos comandos |
| `cmd` | sub | comandos remotos |

## Resultados

Arquitetura:

![Arquitetura](docs/img/arquitetura.png)

Máquina de estados da decisão local:

![Estados](docs/img/estados.png)

Circuito no Wokwi com o nó em CRITICO (HC-SR04 em 50 cm, via fechada):

![Circuito no Wokwi](docs/prints/wokwi_circuito.png)

Mensagens no MQTT WebSocket Client: nó online, comando `fechar` com token, `ack` e novo estado com a cancela fechada:

![Mensagens MQTT](docs/prints/hivemq_mensagens.png)

Dashboard no ThingSpeak recebendo o nível de 278,8 cm publicado pelo nó:

![Dashboard](docs/prints/dashboard_thingspeak.png)

## Observações dos testes

- No Wokwi, o envio HTTP ao ThingSpeak bloqueia o loop por alguns instantes e a simulação roda abaixo da velocidade real, então o MQTT às vezes reconecta logo depois de um `[THINGSPEAK] HTTP 200`. A decisão local continua funcionando e nenhum dado se perde: as janelas passam pelo buffer e o serial mostra `Buffer pendente: 0` e `reenvio concluido`.
