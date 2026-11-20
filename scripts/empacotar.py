"""Monta os pacotes de entrega.

Uso: python scripts/empacotar.py
Gera:
  entrega/GS2026-2_EdgeComputing_SentinelaPirajucara_PlatformIO.zip  só o projeto (item 2 do enunciado)
  entrega/GS2026-2_EdgeComputing_SentinelaPirajucara_Entrega.zip     tudo em um arquivo: relatório,
      projeto, diagramas, prints e LEIA-ME com integrantes e links
"""
import json
import sys
import zipfile
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
FIRMWARE = RAIZ / "firmware"
DESTINO = RAIZ / "entrega" / "GS2026-2_EdgeComputing_SentinelaPirajucara_PlatformIO.zip"

ARQUIVOS = [
    "platformio.ini", "wokwi.toml", "diagram.json", ".vscode/extensions.json",
    "src/main.cpp", "include/borda.h", "include/secrets.h", "include/secrets.exemplo.h",
    "test/test_borda.cpp",
]

secrets = FIRMWARE / "include" / "secrets.h"
if not secrets.exists():
    sys.exit("Falta firmware/include/secrets.h (copie de secrets.exemplo.h e preencha).")
conteudo = secrets.read_text(encoding="utf-8")
if "XXXXXXXXXXXXXXXX" in conteudo or "troque-este-token" in conteudo:
    sys.exit("secrets.h ainda tem os valores de exemplo. Preencha antes de empacotar.")

DESTINO.parent.mkdir(exist_ok=True)
with zipfile.ZipFile(DESTINO, "w", zipfile.ZIP_DEFLATED) as z:
    for rel in ARQUIVOS:
        z.write(FIRMWARE / rel, f"sentinela-pirajucara/{rel}")
    z.write(RAIZ / "README.md", "sentinela-pirajucara/README.md")
print(f"Gerado: {DESTINO.relative_to(RAIZ)}")

# Pacote único de entrega -------------------------------------------------
COMPLETO = RAIZ / "entrega" / "GS2026-2_EdgeComputing_SentinelaPirajucara_Entrega.zip"
PDF = RAIZ / "docs" / "Relatorio_GS_EdgeComputing_SentinelaPirajucara.pdf"
if not PDF.exists():
    sys.exit("Falta o PDF do relatório (rode python docs/gerar_relatorio.py).")
links = json.loads((RAIZ / "docs" / "links.json").read_text(encoding="utf-8"))
if not links.get("video"):
    sys.exit("Falta o link do vídeo em docs/links.json.")

leia_me = f"""SENTINELA PIRAJUÇARA
Global Solution 2026.2 (CitizenTech) - Edge Computing - FIAP

Integrantes
  Gustavo Franzoti Gonçalves - RM566983
  Maykon Santana Fonseca     - RM567041

Links
  Vídeo de demonstração (YouTube, não listado): {links["video"]}
  Repositório: {links["repositorio"]}
  Dashboard público (ThingSpeak): https://thingspeak.mathworks.com/channels/3586730

Conteúdo
  Relatorio_GS_EdgeComputing_SentinelaPirajucara.pdf   relatório técnico (10 páginas)
  sentinela-pirajucara/                                projeto PlatformIO (abrir esta pasta no VS Code)
  imagens/diagramas/                                   arquitetura, estados, ligações e escala
  imagens/prints/                                      Wokwi, HiveMQ e ThingSpeak

Como executar
  1. Instale as extensões PlatformIO IDE e Wokwi Simulator no VS Code.
  2. Abra a pasta sentinela-pirajucara em File > Open Folder.
  3. Compile (pio run ou botão Build) e rode F1 > Wokwi: Start Simulator.
  O secrets.h já está preenchido. Detalhes e comandos MQTT no README.md da pasta do projeto.
"""

with zipfile.ZipFile(COMPLETO, "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("LEIA-ME.txt", leia_me)
    z.write(PDF, PDF.name)
    for rel in ARQUIVOS:
        z.write(FIRMWARE / rel, f"sentinela-pirajucara/{rel}")
    z.write(RAIZ / "README.md", "sentinela-pirajucara/README.md")
    for pasta, destino in (("docs/img", "imagens/diagramas"), ("docs/prints", "imagens/prints")):
        for f in sorted((RAIZ / pasta).glob("*.png")):
            z.write(f, f"{destino}/{f.name}")
print(f"Gerado: {COMPLETO.relative_to(RAIZ)}")
