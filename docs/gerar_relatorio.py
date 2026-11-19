"""Gera o relatório técnico (DOCX) e exporta o PDF pelo Word.

Uso: python docs/gerar_relatorio.py
Prints opcionais em docs/prints/ e links em docs/links.json entram automaticamente.
"""
import json
import os
import re
import sys
from datetime import datetime
from pathlib import Path

from docx import Document
from docx.enum.section import WD_SECTION
from docx.enum.table import WD_TABLE_ALIGNMENT
from docx.enum.text import WD_ALIGN_PARAGRAPH, WD_BREAK, WD_COLOR_INDEX
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Cm, Pt, RGBColor
from pygments import lex
from pygments.lexers import CppLexer, JsonLexer
from pygments.token import Token

RAIZ = Path(__file__).resolve().parent.parent
DOCS = RAIZ / "docs"
IMG = DOCS / "img"
PRINTS = Path(os.environ.get("PRINTS_DIR", DOCS / "prints"))
FONTES = RAIZ / "firmware"
SAIDA_DOCX = DOCS / "Relatorio_GS_EdgeComputing_SentinelaPirajucara.docx"
SAIDA_PDF = SAIDA_DOCX.with_suffix(".pdf")

TITULO = "Sentinela Pirajuçara: nó de borda para alerta de alagamento"
ATIVIDADE = "Global Solution 2026.2 – Edge Computing"
AUTORES = "Gustavo Franzoti Gonçalves; Maykon Santana Fonseca"
INTEGRANTES = [
    ("Gustavo Franzoti Gonçalves", "RM566983", "RM566983@fiap.com.br"),
    ("Maykon Santana Fonseca", "RM567041", "RM567041@fiap.com.br"),
]

FONTE = "Arial"
CINZA_BORDA = "A6A6A6"
FUNDO_CABECALHO = "D9E2F3"

cont = {"tabela": 0, "figura": 0, "codigo": 0}
pendencias = []


# ---------------------------------------------------------------- utilidades
def sombrear(celula, cor):
    tcPr = celula._tc.get_or_add_tcPr()
    shd = OxmlElement("w:shd")
    shd.set(qn("w:val"), "clear")
    shd.set(qn("w:color"), "auto")
    shd.set(qn("w:fill"), cor)
    tcPr.append(shd)


def bordas_tabela(tabela, cor=CINZA_BORDA, tamanho=4):
    tblPr = tabela._tbl.tblPr
    bordas = OxmlElement("w:tblBorders")
    for lado in ("top", "left", "bottom", "right", "insideH", "insideV"):
        b = OxmlElement(f"w:{lado}")
        b.set(qn("w:val"), "single")
        b.set(qn("w:sz"), str(tamanho))
        b.set(qn("w:space"), "0")
        b.set(qn("w:color"), cor)
        bordas.append(b)
    tblPr.append(bordas)


def repetir_cabecalho(linha):
    trPr = linha._tr.get_or_add_trPr()
    el = OxmlElement("w:tblHeader")
    el.set(qn("w:val"), "true")
    trPr.append(el)


def nao_quebrar_linha(linha):
    trPr = linha._tr.get_or_add_trPr()
    el = OxmlElement("w:cantSplit")
    el.set(qn("w:val"), "true")
    trPr.append(el)


def campo(paragrafo, instrucao):
    """Insere um campo do Word (PAGE, TOC...)."""
    r = paragrafo.add_run()
    ini = OxmlElement("w:fldChar"); ini.set(qn("w:fldCharType"), "begin")
    instr = OxmlElement("w:instrText"); instr.set(qn("xml:space"), "preserve"); instr.text = instrucao
    sep = OxmlElement("w:fldChar"); sep.set(qn("w:fldCharType"), "separate")
    txt = OxmlElement("w:t"); txt.text = "1"
    fim = OxmlElement("w:fldChar"); fim.set(qn("w:fldCharType"), "end")
    for el in (ini, instr, sep, txt, fim):
        r._r.append(el)
    return r


def texto_formatado(par, texto):
    """Aceita **negrito** e `código` inline."""
    for parte in re.split(r"(\*\*[^*]+\*\*|`[^`]+`)", texto):
        if not parte:
            continue
        if parte.startswith("**"):
            par.add_run(parte[2:-2]).bold = True
        elif parte.startswith("`"):
            r = par.add_run(parte[1:-1])
            r.font.name = "Consolas"
            r.font.size = Pt(10.5)
        else:
            par.add_run(parte)


# ---------------------------------------------------------------- estilos
def configurar_estilos(doc):
    st = doc.styles
    normal = st["Normal"]
    normal.font.name = FONTE
    normal.font.size = Pt(12)
    normal.element.rPr.rFonts.set(qn("w:eastAsia"), FONTE)
    pf = normal.paragraph_format
    # 1,15 em vez de 1,5: o enunciado limita o relatório a 10 páginas
    pf.line_spacing = 1.15
    pf.space_before = Pt(0)
    pf.space_after = Pt(6)
    pf.first_line_indent = Cm(1.25)
    pf.alignment = WD_ALIGN_PARAGRAPH.JUSTIFY

    for nome, tam, caixa_alta, antes in (("Heading 1", 12, True, 12), ("Heading 2", 12, False, 6)):
        s = st[nome]
        s.font.name = FONTE
        s.font.size = Pt(tam)
        s.font.bold = True
        s.font.italic = False
        s.font.all_caps = caixa_alta
        s.font.color.rgb = RGBColor(0, 0, 0)
        rpr = s.element.get_or_add_rPr()
        fonts = rpr.find(qn("w:rFonts"))
        if fonts is None:
            fonts = OxmlElement("w:rFonts"); rpr.append(fonts)
        for a in ("w:ascii", "w:hAnsi", "w:eastAsia", "w:cs"):
            fonts.set(qn(a), FONTE)
        for a in ("w:asciiTheme", "w:hAnsiTheme", "w:eastAsiaTheme", "w:cstheme"):
            if fonts.get(qn(a)) is not None:
                del fonts.attrib[qn(a)]
        p = s.paragraph_format
        p.space_before = Pt(antes)
        p.space_after = Pt(6)
        p.first_line_indent = Cm(0)
        p.line_spacing = 1.15
        p.keep_with_next = True
        p.alignment = WD_ALIGN_PARAGRAPH.LEFT

    leg = st["Caption"]
    leg.font.name = FONTE
    leg.font.size = Pt(10)
    leg.font.bold = False
    leg.font.italic = False
    leg.font.color.rgb = RGBColor(0, 0, 0)
    leg.paragraph_format.first_line_indent = Cm(0)
    leg.paragraph_format.line_spacing = 1.0
    leg.paragraph_format.space_before = Pt(6)
    leg.paragraph_format.space_after = Pt(4)
    leg.paragraph_format.alignment = WD_ALIGN_PARAGRAPH.CENTER

    for nivel in (1, 2):
        try:
            toc = st[f"TOC {nivel}"]
        except KeyError:
            toc = st.add_style(f"TOC {nivel}", 1)
        toc.font.name = FONTE
        toc.font.size = Pt(12)
        toc.font.bold = nivel == 1
        toc.paragraph_format.first_line_indent = Cm(0)
        toc.paragraph_format.left_indent = Cm(0 if nivel == 1 else 0.6)
        toc.paragraph_format.space_after = Pt(2)
        toc.paragraph_format.line_spacing = 1.5


# ---------------------------------------------------------------- blocos
def par(doc, texto, recuo=True):
    p = doc.add_paragraph()
    texto_formatado(p, texto)
    if not recuo:
        p.paragraph_format.first_line_indent = Cm(0)
    return p


def lista(doc, itens):
    for item in itens:
        p = doc.add_paragraph(style="List Bullet")
        texto_formatado(p, item)
        p.paragraph_format.first_line_indent = Cm(0)
        p.paragraph_format.space_after = Pt(2)
        p.paragraph_format.alignment = WD_ALIGN_PARAGRAPH.JUSTIFY
        for r in p.runs:
            r.font.name = FONTE


def legenda(doc, tipo, texto, fonte=None, acima=True):
    cont[tipo] += 1
    rotulo = {"tabela": "Tabela", "figura": "Figura", "codigo": "Código"}[tipo]
    p = doc.add_paragraph(style="Caption")
    p.add_run(f"{rotulo} {cont[tipo]} – {texto}")
    if acima:
        p.paragraph_format.keep_with_next = True
    if fonte:
        f = doc.add_paragraph(style="Caption")
        f.add_run(f"Fonte: {fonte}")
        f.paragraph_format.space_before = Pt(0)
        if acima:
            f.paragraph_format.keep_with_next = True
    return p


def tabela(doc, titulo, cabecalho, linhas, larguras, fonte="Elaborado pelos autores.", tam=10):
    legenda(doc, "tabela", titulo)
    t = doc.add_table(rows=1, cols=len(cabecalho))
    t.alignment = WD_TABLE_ALIGNMENT.CENTER
    t.autofit = False
    bordas_tabela(t)
    for i, h in enumerate(cabecalho):
        c = t.rows[0].cells[i]
        sombrear(c, FUNDO_CABECALHO)
        c.paragraphs[0].text = ""
        r = c.paragraphs[0].add_run(h)
        r.bold = True
    repetir_cabecalho(t.rows[0])
    for linha in linhas:
        row = t.add_row()
        nao_quebrar_linha(row)
        for i, v in enumerate(linha):
            row.cells[i].paragraphs[0].text = ""
            texto_formatado(row.cells[i].paragraphs[0], v)
    for row in t.rows:
        for i, c in enumerate(row.cells):
            c.width = Cm(larguras[i])
            for p in c.paragraphs:
                p.paragraph_format.first_line_indent = Cm(0)
                p.paragraph_format.line_spacing = 1.0
                p.paragraph_format.space_after = Pt(1)
                p.paragraph_format.alignment = WD_ALIGN_PARAGRAPH.LEFT
                for r in p.runs:
                    r.font.size = Pt(tam)
                    if r.font.name != "Consolas":
                        r.font.name = FONTE
                    else:
                        r.font.size = Pt(tam - 1)
    f = doc.add_paragraph(style="Caption")
    f.add_run(f"Fonte: {fonte}")
    f.paragraph_format.space_before = Pt(2)
    f.paragraph_format.space_after = Pt(8)
    f.paragraph_format.alignment = WD_ALIGN_PARAGRAPH.LEFT
    return t


def figura(doc, caminho, titulo, largura_cm, fonte="Elaborado pelos autores.", altura_max_cm=8.5):
    p = doc.add_paragraph()
    p.paragraph_format.first_line_indent = Cm(0)
    p.paragraph_format.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.keep_with_next = True
    p.paragraph_format.line_spacing = 1.0
    p.paragraph_format.space_after = Pt(0)
    from PIL import Image
    with Image.open(caminho) as im:
        w, h = im.size
    largura_cm = min(largura_cm, altura_max_cm * w / h)
    p.add_run().add_picture(str(caminho), width=Cm(largura_cm))
    legenda(doc, "figura", titulo, acima=False)
    f = doc.paragraphs[-1]
    f.paragraph_format.space_after = Pt(0)
    fo = doc.add_paragraph(style="Caption")
    fo.add_run(f"Fonte: {fonte}")
    fo.paragraph_format.space_before = Pt(0)
    fo.paragraph_format.space_after = Pt(8)


def figura_opcional(doc, nome, titulo, largura_cm, fonte, altura_max_cm=7):
    caminho = next((PRINTS / f"{nome}{ext}" for ext in (".png", ".jpg", ".jpeg") if (PRINTS / f"{nome}{ext}").exists()), None)
    if caminho:
        figura(doc, caminho, titulo, largura_cm, fonte, altura_max_cm)
        return True
    pendencias.append(f"docs/prints/{nome}.png ({titulo})")
    p = par(doc, f"[PENDENTE: salvar o print \"{titulo}\" em docs/prints/{nome}.png e gerar o relatório de novo]", recuo=False)
    for r in p.runs:
        r.font.highlight_color = WD_COLOR_INDEX.YELLOW
    return False


CORES = {
    Token.Keyword: "0000C0", Token.Keyword.Type: "0000C0", Token.Name.Builtin: "0000C0",
    Token.Comment: "3A7D3A", Token.Comment.Single: "3A7D3A", Token.Comment.Preproc: "8B4513",
    Token.Comment.PreprocFile: "8B4513", Token.Literal.String: "A31515",
    Token.Literal.Number: "098658", Token.Name.Function: "795E26", Token.Name.Tag: "0451A5",
    Token.Keyword.Constant: "0000C0",
}


def cor_token(tok):
    while tok is not None:
        if tok in CORES:
            return CORES[tok]
        tok = tok.parent
    return None


def codigo(doc, titulo, fonte_codigo, lexer=None):
    legenda(doc, "codigo", titulo)
    t = doc.add_table(rows=1, cols=1)
    t.alignment = WD_TABLE_ALIGNMENT.CENTER
    bordas_tabela(t, "8C8C8C", 6)
    c = t.rows[0].cells[0]
    c.width = Cm(16)
    sombrear(c, "F5F5F5")
    p = c.paragraphs[0]
    pf = p.paragraph_format
    pf.first_line_indent = Cm(0)
    pf.line_spacing = 1.0
    pf.space_after = Pt(0)
    pf.alignment = WD_ALIGN_PARAGRAPH.LEFT
    for tok, val in lex(fonte_codigo.strip("\n"), lexer or CppLexer()):
        partes = val.split("\n")
        for i, parte in enumerate(partes):
            if i > 0:
                p.add_run().add_break()
            if parte:
                r = p.add_run(parte)
                r.font.name = "Consolas"
                r.font.size = Pt(8.5)
                r._element.rPr.rFonts.set(qn("w:hAnsi"), "Consolas")
                cor = cor_token(tok)
                if cor:
                    r.font.color.rgb = RGBColor.from_string(cor)
    f = doc.add_paragraph(style="Caption")
    f.add_run("Fonte: Elaborado pelos autores (trecho de firmware/src/main.cpp ou firmware/include/borda.h).")
    f.paragraph_format.space_before = Pt(2)
    f.paragraph_format.space_after = Pt(8)
    f.paragraph_format.alignment = WD_ALIGN_PARAGRAPH.LEFT


def trecho(arquivo, inicio, fim):
    """Copia do arquivo-fonte as linhas entre dois marcadores, para o relatório não divergir do código."""
    linhas = (FONTES / arquivo).read_text(encoding="utf-8").splitlines()
    i = next(n for n, l in enumerate(linhas) if inicio in l)
    j = next(n for n, l in enumerate(linhas[i:], i) if fim in l)
    return "\n".join(linhas[i:j + 1])


# ---------------------------------------------------------------- capa e cabeçalho
def capa(doc):
    def linha(texto, tam=12, negrito=False, depois=0, antes=0):
        p = doc.add_paragraph()
        p.paragraph_format.alignment = WD_ALIGN_PARAGRAPH.CENTER
        p.paragraph_format.first_line_indent = Cm(0)
        p.paragraph_format.space_after = Pt(depois)
        p.paragraph_format.space_before = Pt(antes)
        p.paragraph_format.line_spacing = 1.15
        r = p.add_run(texto)
        r.font.size = Pt(tam)
        r.bold = negrito
        return p

    linha("FACULDADE DE INFORMÁTICA E ADMINISTRAÇÃO PAULISTA – FIAP", 12, True)
    linha("Bacharelado em Ciência da Computação", 12)
    linha("Disciplina: Edge Computing", 12, depois=60)
    linha("GLOBAL SOLUTION 2026.2 – CITIZENTECH", 12, True)
    linha("Computação na Borda e um Mundo Sustentável", 12, depois=40)
    linha("SENTINELA PIRAJUÇARA", 16, True)
    linha("Nó de borda com ESP32 para alerta de alagamento e fechamento de via", 13, depois=70)
    for nome, rm, email in INTEGRANTES:
        linha(nome, 12, True)
        linha(f"{rm} · {email}", 11, depois=10)
    linha("Professor: Prof. Dr. Flavio Marques Azevedo", 11, antes=30, depois=110)
    linha("São Paulo", 12, True)
    linha("2026", 12, True)


def cabecalho_rodape(secao):
    secao.different_first_page_header_footer = True
    h = secao.header.paragraphs[0]
    h.text = ""
    h.paragraph_format.first_line_indent = Cm(0)
    h.paragraph_format.alignment = WD_ALIGN_PARAGRAPH.RIGHT
    r = h.add_run(f"{ATIVIDADE} · Sentinela Pirajuçara")
    r.font.size = Pt(9)
    r.font.color.rgb = RGBColor(0x59, 0x59, 0x59)
    pPr = h._p.get_or_add_pPr()
    bdr = OxmlElement("w:pBdr")
    b = OxmlElement("w:bottom")
    for k, v in (("w:val", "single"), ("w:sz", "4"), ("w:space", "1"), ("w:color", CINZA_BORDA)):
        b.set(qn(k), v)
    bdr.append(b)
    pPr.append(bdr)

    f = secao.footer.paragraphs[0]
    f.text = ""
    f.paragraph_format.first_line_indent = Cm(0)
    f.paragraph_format.alignment = WD_ALIGN_PARAGRAPH.RIGHT
    r = f.add_run("Página ")
    r.font.size = Pt(10)
    campo(f, "PAGE").font.size = Pt(10)


def quebra_pagina(doc):
    doc.add_paragraph().add_run().add_break(WD_BREAK.PAGE)


# ---------------------------------------------------------------- conteúdo
def conteudo(doc, links):
    # 1 -------------------------------------------------------------
    doc.add_heading("1 Problema e público (R1)", level=1)
    par(doc, "O córrego Pirajuçara corre ao longo da Av. Eliseu de Almeida, na zona oeste de São Paulo, "
             "e transborda com frequência nas chuvas de verão. Quando isso acontece, a água chega às ruas "
             "transversais e às pontes estreitas que ligam os dois lados da avenida antes de qualquer aviso "
             "oficial alcançar quem está passando. Motoristas entram na via alagada, pedestres atravessam a "
             "ponte com a água subindo e os comerciantes da margem perdem tempo que seria usado para levantar "
             "mercadorias.")
    par(doc, "O **Sentinela Pirajuçara** é um nó de borda instalado sob uma dessas pontes. Ele mede o nível da "
             "água, estima a intensidade da chuva e, sozinho, decide o nível de risco: avisa no painel LCD e no "
             "LED, toca a sirene e fecha uma cancela que bloqueia a entrada da ponte quando o nível fica crítico. "
             "O público beneficiado é formado pelos pedestres e motoristas que usam a travessia, pelos moradores "
             "e comerciantes das quadras vizinhas e pela Defesa Civil municipal, que recebe os dados e os eventos "
             "pelo broker MQTT.")
    par(doc, "Como indicador mensurável adotamos a **antecedência do alerta**: minutos entre o disparo do ALERTA "
             "e o momento em que o nível atinge 220 cm. No cenário simulado do teste automatizado "
             "(`testeCenarioEnchente`, subida de 5 cm/min), a regra de taxa de subida disparou o ALERTA 842 s "
             "(14 min) antes do que um alarme só por nível. Também acompanhamos o número de alertas emitidos por "
             "evento de chuva e a redução de mensagens enviadas à nuvem (seção 8).")
    par(doc, "A decisão precisa ficar na borda porque a chuva forte é justamente o momento em que energia, "
             "Wi-Fi e rede celular costumam falhar. Fechar a cancela não pode esperar uma volta pela nuvem nem "
             "depender de o broker estar acessível. Na borda o nó reage em segundos, continua funcionando sem "
             "rede e envia para a nuvem só o que interessa.")

    # 2 -------------------------------------------------------------
    doc.add_heading("2 Sensores, atuadores e circuito (R2)", level=1)
    par(doc, "O protótipo roda no Wokwi com um ESP32 DevKit C v4. São quatro entradas e quatro saídas, cada uma "
             "ligada a uma parte do problema. O potenciômetro **simula** um pluviômetro de báscula: 0 a 4095 do "
             "ADC vira 0 a 100 mm/h. A chave deslizante não conta como sensor; ela existe só para demonstrar a "
             "queda de rede no vídeo.")
    tabela(doc, "Componentes do nó e função no problema",
           ["Componente", "Pinos", "Função no problema"],
           [["HC-SR04 (sensor)", "TRIG 5, ECHO 18", "Mede a distância até a lâmina d'água; nível = 350 cm − distância."],
            ["Potenciômetro (sensor)", "GPIO34", "Simula a intensidade da chuva (mm/h), usada para antecipar o alerta."],
            ["DHT22 (sensor)", "GPIO15", "Temperatura corrige a velocidade do som no HC-SR04; umidade vai para o histórico."],
            ["Botão (sensor)", "GPIO4", "Agente no local silencia a sirene; ela volta se o estado piorar."],
            ["LCD 16x2 I2C (saída)", "SDA 21, SCL 22", "Painel para quem passa: estado, nível e \"VIA FECHADA\"."],
            ["LED RGB (saída)", "25, 26, 33", "Verde, amarelo, vermelho piscando e vermelho fixo, visível de longe."],
            ["Buzzer (saída)", "GPIO13", "Bipe no ALERTA e sirene alternada no CRITICO."],
            ["Servo (saída)", "GPIO19", "Cancela da ponte: fecha a 0° no CRITICO, aberta a 90°."]],
           [4.0, 3.0, 9.0])
    figura(doc, IMG / "ligacoes.png", "Ligações do circuito", 13, altura_max_cm=4)
    figura_opcional(doc, "wokwi_circuito", "Circuito montado no Wokwi", 14,
                    "Captura de tela do simulador Wokwi feita pelos autores.", altura_max_cm=6)
    par(doc, "Buzzer e servo usam canais LEDC em timers diferentes (canal 6 e canal 4) porque trabalham em "
             "frequências diferentes; no mesmo timer, ligar a sirene mudaria a frequência do servo. Numa "
             "montagem física o ECHO do HC-SR04 precisaria de divisor de tensão para 3,3 V.")

    # 3 -------------------------------------------------------------
    doc.add_heading("3 Decisão na borda (R3)", level=1)
    par(doc, "Toda a lógica de decisão fica em `include/borda.h`, separada do código de hardware para ser "
             "testada no computador. O fluxo a cada leitura é: o HC-SR04 é lido a cada 500 ms, a distância é "
             "corrigida pela temperatura, o nível passa por uma **média móvel de 8 amostras** (4 s) e o "
             "resultado é classificado em quatro estados com **histerese** de 10 cm. Para subir de estado basta "
             "cruzar o limiar; para descer, o nível precisa ficar 10 cm abaixo dele. Sem isso, a água oscilando "
             "em 220 cm faria a sirene ligar e desligar a cada segundo.")
    figura(doc, IMG / "estados.png", "Máquina de estados da decisão local", 16)
    par(doc, "Sobre o estado por nível entra a **antecipação**: a cada 5 s o nível filtrado é guardado num "
             "histórico de 60 s e o nó calcula a taxa de subida. Se o estado é ATENCAO e a água sobe 4 cm/min "
             "ou mais, ou a chuva passa de 50 mm/h, o nó já declara ALERTA. No CRITICO a cancela fecha mesmo "
             "que um comando remoto tenha pedido abertura, e um pedido de abertura nesse estado é recusado.")
    codigo(doc, "Classificação do nível com histerese",
           trecho("include/borda.h", "inline Estado classificarNivel", "return static_cast<Estado>(e);") + "\n}")
    par(doc, "Para reduzir tráfego, as leituras são **agregadas** em janelas de 30 s (média, mínimo e máximo) e "
             "passam por um **deadband**: em estado NORMAL a janela só é publicada se o nível variou 2 cm ou "
             "mais, ou a cada 5 min como sinal de vida. Fora do NORMAL toda janela é publicada, e mudanças de "
             "estado viram um evento imediato no tópico `estado`. O arquivo `test/test_borda.cpp` tem 48 "
             "verificações (filtro, fila, histerese, antecipação, deadband) e todas passaram. No cenário de "
             "2 h de enchente simulada, das 31.680 amostras brutas só 505 mensagens seriam publicadas, "
             "redução de 98,41%.")

    # 4 -------------------------------------------------------------
    doc.add_heading("4 Comunicação MQTT (R4)", level=1)
    par(doc, "O broker é o HiveMQ Cloud. A hierarquia segue `citizentech/<grupo>/<local>/<dispositivo>/<grandeza>`, "
             "com base `citizentech/gs-gm/pirajucara-eliseu/no01/`. São sete tópicos de publicação e um de "
             "assinatura, todos com payload JSON.")
    tabela(doc, "Tópicos MQTT do nó (prefixo citizentech/gs-gm/pirajucara-eliseu/no01/)",
           ["Tópico", "Sentido", "Quando", "Exemplo de payload"],
           [["nivel", "pub", "fim da janela (deadband)",
             '`{"ts":1794985200,"nivel_cm":231.4,"min":228.9,"max":233.0,"taxa_cm_min":4.6,"estado":2,"amostras":60,"buffer":false}`'],
            ["chuva", "pub", "fim da janela", '`{"ts":1794985200,"chuva_mm_h":58.2,"max":61.0,"simulado":true,"buffer":false}`'],
            ["ambiente", "pub", "fim da janela", '`{"ts":1794985200,"temp_c":22.4,"umid_pct":91.0,"buffer":false}`'],
            ["estado", "pub (retido)", "mudança de estado",
             '`{"ts":1794985212,"estado":"ALERTA","codigo":2,"motivo":"taxa_subida","nivel_cm":214.0,"cancela":"aberta","sirene":"ativa"}`'],
            ["status", "pub (retido, LWT)", "conexão / queda", '`{"status":"online","fw":"1.0.0","rssi":-58,"buffer_pendente":0}` ou `{"status":"offline"}`'],
            ["diagnostico", "pub", "a cada 5 min", '`{"amostras":4410,"publicadas":96,"evitadas":4290,"buffer":0,"reconexoes":2}`'],
            ["ack", "pub", "resposta a comando", '`{"acao":"cancela","ok":false,"msg":"nivel critico, abertura bloqueada"}`'],
            ["cmd", "sub (QoS 1)", "comando remoto", '`{"acao":"cancela","modo":"fechar"}`']],
           [2.2, 2.4, 2.8, 8.6], tam=9)
    par(doc, "O tópico `cmd` aceita cinco ações que mudam o comportamento do nó: `silenciar` (desliga a sirene "
             "até o estado piorar), `cancela` com modo `auto`, `fechar` ou `abrir`, `limiares` (novos valores "
             "de atenção, alerta, crítico e histerese, validados antes de aplicar), `intervalo` (janela de "
             "agregação entre 10 e 300 s) e `diagnostico`. Cada comando recebe resposta em `ack`, inclusive "
             "quando é recusado.")
    figura_opcional(doc, "hivemq_mensagens", "Mensagens do nó chegando no HiveMQ Cloud (Web Client)", 15,
                    "Captura de tela do HiveMQ Cloud feita pelos autores.", altura_max_cm=5)

    # 5 -------------------------------------------------------------
    doc.add_heading("5 Dashboard (R5)", level=1)
    par(doc, "Escolhemos o ThingSpeak. O nó envia a cada 20 s, por HTTP, os oito campos do canal (o plano "
             "gratuito aceita uma atualização a cada 15 s). O campo 5 carrega o estado decidido na borda e "
             "alimenta um indicador do tipo lâmpada, que acende a partir de 2 (ALERTA). O medidor de nível usa "
             "as mesmas faixas dos limiares, para a equipe da Defesa Civil ler o risco sem conhecer o código.")
    tabela(doc, "Campos do canal ThingSpeak",
           ["Campo", "Grandeza", "Widget"],
           [["field1", "Nível filtrado (cm)", "Gauge 0–350 com faixas 150/220/280 e gráfico"],
            ["field2", "Chuva (mm/h, simulada)", "Gráfico de linha"],
            ["field3 / field4", "Temperatura (°C) / umidade (%)", "Numeric display"],
            ["field5", "Estado da borda (0 a 3)", "Lamp indicator (acende se ≥ 2) e gráfico"],
            ["field6", "Taxa de subida (cm/min)", "Gráfico de linha"],
            ["field7 / field8", "Mensagens evitadas / ocupação do buffer", "Numeric display"]],
           [3.0, 6.0, 7.0])
    figura_opcional(doc, "dashboard_thingspeak", "Dashboard do canal no ThingSpeak", 15.5,
                    "Captura de tela do ThingSpeak feita pelos autores.", altura_max_cm=6)

    # 6 -------------------------------------------------------------
    doc.add_heading("6 Segurança e LGPD (R6)", level=1)
    par(doc, "A conexão com o HiveMQ Cloud usa TLS na porta 8883 e o ESP32 valida o certificado do servidor "
             "com a CA raiz ISRG Root X1, em vez de `setInsecure()`. O acesso é feito com usuário e senha "
             "criados só para a GS. Wi-Fi, host, usuário, senha, chave de escrita do ThingSpeak e o certificado "
             "ficam em `include/secrets.h`, que está no `.gitignore`; o repositório traz apenas "
             "`secrets.exemplo.h`. O Last Will também protege a operação: se o nó cair, o broker publica "
             "`offline` e a central sabe que aquela ponte está sem monitoramento remoto.")
    par(doc, "Os comandos remotos são validados no próprio nó. Limiares fora de ordem ou acima da altura do "
             "sensor são recusados, o intervalo tem faixa fixa e a cancela não abre com nível crítico, então "
             "nem um comando errado nem uma credencial vazada conseguem liberar a ponte durante a enchente. "
             "Uma limitação conhecida é o envio ao ThingSpeak por HTTP sem TLS, aceitável aqui porque só "
             "trafegam medições ambientais, mas a troca por HTTPS seria o próximo passo. Em produção, o usuário "
             "do broker ficaria restrito por ACL ao prefixo `citizentech/gs-gm/#`.")
    par(doc, "Quanto à LGPD (Lei nº 13.709/2018), o nó coleta nível da água, intensidade da chuva, temperatura, "
             "umidade, estado de alerta, posição da cancela e dados técnicos do próprio equipamento (IP local, "
             "RSSI, contadores). Nenhum desses dados identifica ou torna identificável uma pessoa natural, por "
             "isso não são dados pessoais nos termos do art. 5º, I. O acionamento do botão é registrado sem "
             "qualquer identificação de quem apertou. Se o projeto evoluísse para câmera na ponte, passaria a "
             "tratar dados pessoais e precisaria de base legal (art. 7º, III, execução de políticas públicas), "
             "aviso visível no local e retenção mínima.")

    # 7 -------------------------------------------------------------
    doc.add_heading("7 Estratégia de resiliência (R7)", level=1)
    par(doc, "O `loop()` nunca espera a rede. `gerenciarWifi()` chama `WiFi.begin()` e só confere o status nas "
             "voltas seguintes, repetindo a tentativa a cada 15 s. `gerenciarMqtt()` faz no máximo uma tentativa "
             "por vez, com intervalo que dobra de 2 s até 30 s. A única espera real é o handshake TLS dentro de "
             "`mqtt.connect()`, limitada a 8 s pelo `setHandshakeTimeout`; durante esse tempo os atuadores "
             "mantêm o último estado, então a cancela e a sirene não mudam de comportamento.")
    codigo(doc, "Reconexão MQTT com backoff e Last Will",
           trecho("src/main.cpp", "// Uma única tentativa por vez", "backoffMqtt = min(backoffMqtt * 2"))
    par(doc, "O Last Will é registrado na conexão no tópico `status`, retido, com `{\"status\":\"offline\"}`; ao "
             "conectar, o nó publica `online` no mesmo tópico. Para o teste, a chave deslizante derruba o Wi-Fi "
             "sem mandar DISCONNECT, como numa queda real, e o broker publica o Last Will após o keepalive de "
             "10 s. Enquanto o link está fora, cada janela de 30 s entra numa **fila circular de 60 posições** "
             "(30 min de queda) marcada com `buffer:true` e com o horário original. Na reconexão, "
             "`esvaziarBuffer()` reenvia três janelas por volta do loop, na ordem em que foram geradas, e só "
             "remove da fila o que foi publicado com sucesso. Se a queda passar de 30 min, a janela mais antiga "
             "é descartada e contada no diagnóstico.")

    # 8 -------------------------------------------------------------
    doc.add_heading("8 Arquitetura e análise de escala (R8)", level=1)
    figura(doc, IMG / "arquitetura.png", "Arquitetura sensor → borda → broker → dashboard", 12, altura_max_cm=5.5)
    par(doc, "Para 100 nós espalhados pelas bacias da cidade, a hierarquia de tópicos ganha o nível da bacia "
             "no campo `<local>` (ex.: `pirajucara-eliseu`, `aricanduva-ponte3`). Com isso a central da Defesa "
             "Civil assina `citizentech/gs-gm/+/+/estado` e recebe só os eventos de todos os nós, e cada "
             "gateway assina apenas a sua bacia com `citizentech/gs-gm/pirajucara-+/#`.")
    figura(doc, IMG / "escala.png", "Organização para 100 dispositivos com gateways fog por bacia", 11, altura_max_cm=4.6)
    par(doc, "O gateway fog (um Raspberry Pi ou mini PC por bacia, com broker local) recebe os nós por MQTT na "
             "rede da região, consolida a bacia (maior nível, quantos nós em ALERTA, tendência) e repassa à "
             "nuvem só esse resumo e os eventos. Isso reduz as conexões no HiveMQ Cloud de 100 para 4, o que "
             "importa porque o plano gratuito Serverless é limitado a 100 conexões, e deixa um único canal do "
             "ThingSpeak por bacia. A Tabela 4 compara o envio de cada amostra com o envio feito pela borda.")
    tabela(doc, "Estimativa de mensagens por hora (nível e chuva a 2 Hz, DHT22 a 0,4 Hz)",
           ["Cenário", "1 nó", "100 nós", "Redução"],
           [["Envio de toda amostra (3 tópicos)", "15.840", "1.584.000 (440 msg/s)", "–"],
            ["Borda em NORMAL (deadband + diagnóstico)", "48", "4.800", "99,7%"],
            ["Borda com todos os nós em ALERTA", "372 + eventos", "37.200", "97,7%"],
            ["Gateways → nuvem (4 bacias, resumo a cada 60 s)", "–", "240 + eventos", "99,98%"]],
           [6.6, 2.6, 4.2, 2.6])
    par(doc, "Com payload médio de 250 bytes já contando cabeçalhos MQTT e TLS, os 100 nós enviando direto em "
             "estado NORMAL geram cerca de 3,5 milhões de mensagens e 0,9 GB por mês, contra mais de 1,1 bilhão "
             "de mensagens e cerca de 285 GB no envio de toda amostra. No ThingSpeak, 4 canais de bacia "
             "atualizados a cada 60 s somam 2,1 milhões de mensagens por ano, dentro do limite de 3 milhões "
             "do plano gratuito, o que não seria possível com 100 canais a cada 20 s.")

    # 9 -------------------------------------------------------------
    doc.add_heading("9 Declaração de uso de IA", level=1)
    par(doc, "Usamos o Claude (Anthropic) como apoio em três partes: na escrita inicial do firmware e dos "
             "testes de `borda.h`, na montagem dos diagramas em Mermaid e na revisão do texto deste relatório. "
             "O grupo definiu o problema, o local e os limiares, conferiu cada trecho de código, compilou o "
             "projeto no PlatformIO, rodou os testes, criou as contas e as credenciais no HiveMQ Cloud e no "
             "ThingSpeak, testou o circuito no Wokwi, fez os prints e gravou o vídeo. Ajustes feitos pelo grupo "
             "sobre o material de apoio estão registrados no histórico de commits do repositório.")

    # 10 ------------------------------------------------------------
    doc.add_heading("10 Links do projeto e do vídeo", level=1)
    itens = [("Projeto no Wokwi", "wokwi"), ("Vídeo de demonstração (YouTube, não listado)", "video"),
             ("Repositório do código-fonte", "repositorio")]
    for rotulo, chave in itens:
        p = doc.add_paragraph()
        p.paragraph_format.first_line_indent = Cm(0)
        p.paragraph_format.space_after = Pt(2)
        p.add_run(f"{rotulo}: ").bold = True
        valor = (links.get(chave) or "").strip()
        if valor:
            p.add_run(valor)
        else:
            r = p.add_run(f"[PENDENTE: preencher \"{chave}\" em docs/links.json]")
            r.font.highlight_color = WD_COLOR_INDEX.YELLOW
            pendencias.append(f"link '{chave}' em docs/links.json")

    # Referências ---------------------------------------------------
    doc.add_heading("Referências", level=1)
    refs = [
        "BRASIL. **Lei nº 13.709, de 14 de agosto de 2018**. Lei Geral de Proteção de Dados Pessoais (LGPD). "
        "Brasília, DF: Presidência da República, 2018. Disponível em: "
        "https://www.planalto.gov.br/ccivil_03/_ato2015-2018/2018/lei/l13709.htm. Acesso em: 18 nov. 2026.",
        "ESPRESSIF SYSTEMS. **Arduino-ESP32 documentation**: LEDC, WiFi e WiFiClientSecure. 2024. "
        "Disponível em: https://docs.espressif.com/projects/arduino-esp32/en/latest/. Acesso em: 18 nov. 2026.",
        "HIVEMQ. **HiveMQ Cloud documentation**. Disponível em: https://docs.hivemq.com/hivemq-cloud/. "
        "Acesso em: 18 nov. 2026.",
        "HIVEMQ. **MQTT Essentials**: Last Will and Testament; retained messages; keep alive. Disponível em: "
        "https://www.hivemq.com/mqtt/. Acesso em: 18 nov. 2026.",
        "MATHWORKS. **ThingSpeak documentation**: write data with REST API; license limits. Disponível em: "
        "https://www.mathworks.com/help/thingspeak/. Acesso em: 18 nov. 2026.",
        "O'LEARY, N. **PubSubClient**: a client library for MQTT messaging. Versão 2.8. Disponível em: "
        "https://github.com/knolleary/pubsubclient. Acesso em: 18 nov. 2026.",
        "BLANCHON, B. **ArduinoJson**. Versão 7. Disponível em: https://arduinojson.org/. Acesso em: 18 nov. 2026.",
        "WOKWI. **Wokwi documentation**: ESP32 simulation, WiFi networking e diagram.json. Disponível em: "
        "https://docs.wokwi.com/. Acesso em: 18 nov. 2026.",
    ]
    for ref in refs:
        p = doc.add_paragraph()
        texto_formatado(p, ref)
        p.paragraph_format.first_line_indent = Cm(0)
        p.paragraph_format.line_spacing = 1.0
        p.paragraph_format.space_after = Pt(6)
        p.paragraph_format.alignment = WD_ALIGN_PARAGRAPH.LEFT


# ---------------------------------------------------------------- montagem
def montar():
    links_arq = DOCS / "links.json"
    links = json.loads(links_arq.read_text(encoding="utf-8")) if links_arq.exists() else {}

    doc = Document()
    configurar_estilos(doc)
    sec = doc.sections[0]
    sec.page_height, sec.page_width = Cm(29.7), Cm(21.0)
    sec.top_margin, sec.left_margin = Cm(3), Cm(3)
    sec.bottom_margin, sec.right_margin = Cm(2), Cm(2)
    sec.header_distance, sec.footer_distance = Cm(1.5), Cm(1.0)
    cabecalho_rodape(sec)

    capa(doc)
    quebra_pagina(doc)

    t = doc.add_paragraph()
    t.paragraph_format.alignment = WD_ALIGN_PARAGRAPH.CENTER
    t.paragraph_format.first_line_indent = Cm(0)
    t.paragraph_format.space_after = Pt(12)
    r = t.add_run("SUMÁRIO"); r.bold = True
    toc = doc.add_paragraph()
    toc.paragraph_format.first_line_indent = Cm(0)
    campo(toc, 'TOC \\o "1-2" \\h \\z \\u')
    quebra_pagina(doc)

    conteudo(doc, links)

    cp = doc.core_properties
    cp.author = AUTORES
    cp.last_modified_by = "Maykon Santana Fonseca"
    cp.title = f"{TITULO} – {ATIVIDADE}"
    cp.subject = "Global Solution 2026.2 – Edge Computing – FIAP"
    cp.keywords = "ESP32; MQTT; HiveMQ; ThingSpeak; edge computing; alagamento"
    cp.comments = ""
    cp.category = ""
    cp.created = cp.modified = datetime.now()
    cp.revision = 1
    doc.save(SAIDA_DOCX)


def exportar_pdf():
    """Abre no Word para atualizar o sumário e os campos de página e exporta o PDF."""
    import win32com.client
    word = win32com.client.DispatchEx("Word.Application")
    word.Visible = False
    word.DisplayAlerts = 0
    try:
        d = word.Documents.Open(str(SAIDA_DOCX))
        d.Fields.Update()
        for toc in d.TablesOfContents:
            toc.Update()
        d.Repaginate()
        paginas = d.ComputeStatistics(2)  # wdStatisticPages
        d.Save()
        d.ExportAsFixedFormat(OutputFileName=str(SAIDA_PDF), ExportFormat=17,  # wdExportFormatPDF
                              IncludeDocProps=True, CreateBookmarks=1)
        d.Close(False)
    finally:
        word.Quit()
    # O Word grava o nome do usuário do Windows em "modificado por"; volta para o integrante
    doc = Document(SAIDA_DOCX)
    doc.core_properties.last_modified_by = "Maykon Santana Fonseca"
    doc.core_properties.author = AUTORES
    doc.save(SAIDA_DOCX)
    return paginas


if __name__ == "__main__":
    montar()
    paginas = exportar_pdf() if "--sem-pdf" not in sys.argv else None
    print(f"DOCX: {SAIDA_DOCX.name}")
    if paginas:
        print(f"PDF:  {SAIDA_PDF.name} ({paginas} páginas)")
    for p in pendencias:
        print(f"Pendente: {p}")
