"""Sentences for the abbreviation, Roman-numeral and contact-routing paths.

The sentences come from tests/test_codeswitch_abbreviations.py at the
reference tag and from the review probes of TigreGotico/tugaphone#183: English
words with a sentence period ("street art."), the pronoun "I" after a marker,
markers before Roman numerals and digits, brackets and guillemets, initials,
and Roman numerals after names and nouns of both genders.
"""
import ast

from tugaphone import text_normalization as tn

from . import common

PROBES = [
    # English words with a sentence period, and markers that a name follows
    "Gosto muito de pop art.", "Ela adora street art.", "Ela usa sempre um cap.", "He wore a cap.",
    "I love modern art.", "I ate a fig.", "Comi uma fig.", "see Fig. 3 for details", "Ele disse: see Fig. 3.",
    "I met Art. He was nice.", "Art. Nouveau é um estilo.", "Art. 5 da lei.", "Ele disse Batman Vs. Superman",
    "Batman vs. Superman estreou.", "o livro de George R. R. Martin", "Charles D. Smith chegou", "o filme Dr. Who",
    "Tel. 21 000 000", "Tel. Aviv é uma cidade.", "Séc. XIX foi longo.", "Pág. 4",
    "O Cap. Salgueiro Maia saiu do quartel.", "o capitão: Cap. Haddock", "li o Cap. XII.", "li o Cap. I.",
    "de D. Maria I no baú", "D. João VI chegou ao Brasil", "o Papa Pio X morreu", "no século XXI",
    "o art. 5.º da lei", "C'est l'art. Voilà.", "Es un arte, no un art.", "Le Dr. Martin est là.",
    "El Sr. García llegó.", "Vive na Av. da Liberdade.", "Ela tem um S. no nome", "V. Ex.ª chegou",
    "o Sr. Silva e a Sra. Costa", "the Prof. said hi", "Eng. Silva", "Usa Lx. para Lisboa", "Ele é fã de fan art.",
    "Ele vive em Tel Aviv.", "the cap. was red",
    "Eu jogo com os elementos da natureza quando pratico land art.", "a vitamina D. Depois", "S. Pedro",
    # digits and Roman numerals after markers, initials, titles
    "see Fig. 3.", "I love street art. I paint every day.", "I wore a cap. I liked it.",
    "He loves pop art. I do not.", "Ela adora street art. I mean it.", "Comprei um cap. 2 dias depois perdi-o.",
    "Warhol fez pop art. 1962 foi o ano.", "street art. V for Vendetta", "I love art. X marks the spot.",
    "Ele viu o Dr. Who ontem.", "the Dr. said no", "a Sala III", "na Sala III", "ver Fig. II", "ver fig. II",
    "a Pág. IV", "ver pág. IV", "o Anexo II", "a Tabela III", "a Fase II", "ver página IV", "a página 4",
    "a sala 3", "a figura 2", "o anexo 2", "a tabela 3", "Isabel II", "a Rainha Isabel II", "D. Maria II",
    "o Papa Bento XVI", "o Papa Paulo VI", "Henrique VIII", "Afonso III", "a Guerra II", "a Parte II",
    "o Tomo III", "Ramsés II", "Catarina II", "a Grande Guerra II", "(D. Maria II)", "«D. João V»",
    "(século XIX)", "o (Cap. II)", "«Cap. XII».", "S. Ex.ª chegou", "V. Ex.ª, obrigado", "Vossa Ex.ª",
    "R. Augusta", "Av. da Liberdade", "Tel. 912 345 678", "Séc. V", "séc. XXI", "Págs. 4 a 6", "Art. 5.º",
    "Art. 5", "Art. Nouveau", "Conheci o Art.", "Conheci o Art. 5 vezes", "D. Sebastião", "D. Pedro",
    "John V. Smith", "Part V.", "Cap. IV", "o rei D. Dinis.", "Morreu em Cap. Verde",
    # a single I, V or X next to Portuguese and English neighbours
    "No Cap. I do livro", "li o cap. I.", "Cap. XII.", "(Cap. XII)", "Cap. X", "ver cap. V e VI", "li o Cap. X.",
    "Tenho 23 anos, Sr. Silva; li o Cap. XII.", "Cap. I", "Cap. V", "Cap. I descreve a guerra",
    "Cap. V descreve a guerra", "o Cap. V", "o cap. X", "li o cap.", "li o Cap.", "o capítulo é o cap. V",
    "ver o Cap. V e o Cap. X", "No Cap. V do livro", "No Cap. X do livro", "Leia o Cap. V do livro.",
    "Leia o Cap. X do livro.", "Leia o Cap. I do livro.", "Veja o art. V da lei.", "o Cap. V vale a pena",
    "o Cap. X vale a pena", "o Cap. I weekend", "the Cap. I chapter", "no Cap. I, a guerra", "no Cap. V: a guerra",
    "no Cap. X; a guerra", "(Cap. V)", "«Cap. I»", "ver (cap. X) e (art. V)", "a Fig. I mostra", "a fig. V mostra",
    "a pág. X tem", "o séc. V viu", "no Séc. X houve", "ler Cap. I me ajuda", "ler Cap. I come bem", "Cap. I come",
    "come Cap. I", "e Cap. I e", "o Cap. I a", "do Cap. V ao Cap. X", "in the Cap. X building",
    "Cap. X marks the spot", "Ela adora street art. X marks the spot.", "I wore a cap. I.", "I love art. V.",
    "Comprei um cap. 2 dias depois",
    # the English pronoun after a marker, one or two words from a foreign word
    "I wore a cap. I think it suits me.", "She gave me a cap. I wear it daily.", "He lost his cap. I found it.",
    "Nice cap. I want one.", "I bought a new cap. I am happy.", "Take off your cap. I mean it.",
    "My cap. I need it back.", "I studied art. I was good at it.", "This is modern art. I do not get it.",
    "We saw some art. I have photos.", "It is art. I know it.", "ver o cap. I e o II", "o art. I da lei",
    "no Cap. V vemos", "o Cap. X termina", "Cap. I, secção 2", "Leia o cap. I antes", "segundo o art. V do código",
    "(Cap. I)", "veja o cap. I hoje", "Leia o cap. I antes de sair", "o Cap. I do Harry Potter",
    "no Cap. V o software falha", "o meu download do Cap. I",
]

# Names and nouns before a Roman numeral: rulers, popes, female names, and
# label and institution nouns of both genders.
HEADS = (
    "Pio Bento Leão Urbano Clemente Inocêncio Gregório Sisto Bonifácio Celestino Adriano Paulo João Martinho Nicolau "
    "Calisto Alexandre Eugénio Félix Estêvão Sancho Ramiro Ordonho Vermudo Fruela Garcia Teresa Urraca Sancha "
    "Berenguela Leonor Filipa Beatriz Constança Mafalda Branca Isabel Joana Luís Carlos Jaime Pedro Manuel Duarte "
    "Dinis Fernando Henrique Rodrigo Afonso Ricardo Jorge Guilherme Eduardo Frederico Napoleão Constantino "
    "Justiniano Teodósio Ptolomeu Cleópatra Ramsés Tutmés Amenófis Vitória Ana Maria Mariana Elisabete Rosa Luz "
    "Glória Graça Piedade Paz Cruz Fé Esperança Amadeu Humberto Vítor Óscar Gustavo Cristiano Cristina Olavo Haakon "
    "Canuto Valdemar Érico Magno Igreja Ponte Torre Casa Escola Avenida Rua Guerra Fase Sala Tabela Figura Página "
    "Alínea Linha Coluna Classe Série Edição Lei Carta Bula Liga Divisão Ordem Dinastia Cruzada Sinfonia Missa "
    "Conferência Cimeira Bienal Olimpíada Taça"
).split()


def test_sentences(repo):
    """The string literals of the #183 test file at the reference tag."""
    source = common.test_sources(repo)["tests/test_codeswitch_abbreviations.py"]
    return [node.value for node in ast.walk(ast.parse(source))
            if isinstance(node, ast.Constant) and isinstance(node.value, str) and node.value.strip()]


def sentences(repo):
    return common.unique(test_sentences(repo) + PROBES)


def roman_heads():
    out = []
    for head in HEADS:
        for numeral in ("II", "VII", "XII"):
            out += [f"{head} {numeral}", f"a {head} {numeral}", f"«{head} {numeral}»", f"({head}) {numeral}"]
        out.append(f"{head.lower()} III")
    return common.unique(out)


def marker_grid():
    """Each marker and abbreviation before a digit, a numeral, a name or
    nothing, inside brackets, guillemets and quotes."""
    markers = sorted(tn._CAPITALISED_MARKERS) + sorted(tn._REFERENCE_MARKERS) + [
        "CAP.", "ART.", "D.", "S.", "V.", "R.", "Dr.", "Vs.", "vs."]
    # "²" and "①" are digits for str.isdigit but not decimals.
    following = ["3", "5.º", "XII", "I", "V", "X", "IV", "Maria", "x", "Ex.ª", "²", "①", ""]
    out = []
    for marker in markers:
        for word in following:
            for lead, trail in (("", ""), ("(", ")"), ("«", "»"), ("“", "”"), ("", "."), ("", ","), ("«", "».")):
                body = f"{marker} {word}".strip()
                out += [f"o {lead}{body}{trail} e", f"{lead}{body}{trail}"]
    return common.unique(out)
