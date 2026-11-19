"""Monta o .zip do projeto PlatformIO pedido na entrega (item 2 do enunciado).

Uso: python scripts/empacotar.py
Gera entrega/GS2026-2_EdgeComputing_SentinelaPirajucara_PlatformIO.zip
"""
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
