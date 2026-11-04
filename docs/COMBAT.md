# Esquivas, agarrões e reversões

Mapa inicial de engenharia reversa do `SLUS_212.09` NTSC-U, SHA-256
`60399090fc69ffdb4c39afd30977ab50890b0e33ed6bd9374e4d9497f4246081`.
Os nomes descritivos abaixo são hipóteses de trabalho; os identificadores `sub_*`
e endereços são os do executável, listados em `recomp/urban_functions.csv`.
Os arquivos em `generated/` são locais, produzidos pela recompilação.

A defesa de agarrão e a resposta à reversão usam o mesmo caminho. A janela depende
do movimento do atacante, mas a resposta depois de uma reversão recebe também um
contador de valor inicial 4. Esse contador é decrementado antes de aceitar o
comando: permite entrada nas três primeiras verificações elegíveis, caso as outras
condições continuem válidas. Não há uma janela universal de quatro frames.

## Mapa de funções

| Função | Papel observado |
|---|---|
| `sub_00243A18`, `0x243A18` | Atualiza os lutadores na ordem dos slots e depois resolve os contatos. |
| `sub_0024D560`, `0x24D560` | Decide se o estado atual permite abrir a janela de esquiva. |
| `sub_0024D610`, `0x24D610` | Lê o comando de esquiva, abre/decrementa o contador e registra a direção. |
| `sub_0024D790`, `0x24D790` | Consulta se o contador da esquiva está positivo. |
| `sub_00250480`, `0x250480` | Resolve contato de golpe: dano, esquiva ou defesa por outros estados. |
| `sub_0024DF70`, `0x24DF70` | Executa a defesa: evasão individual ou animações de contra-ataque em dupla. |
| `sub_0024B890`, `0x24B890` | Outro caminho de esquiva, condicionado ao tipo de movimento 3. |
| `sub_0024B8F0`, `0x24B8F0` | Inicia o movimento de defesa direcional do banco do personagem. |
| `sub_0024A980`, `0x24A980` | Processa o comando de agarrar, escolhe alvo e movimento conforme o contexto. |
| `sub_0024A058`, `0x24A058` | Estabelece o par atacante/vítima e inicia as animações do agarrão. |
| `sub_0024B120`, `0x24B120` | Verifica janela, comando e direção da defesa de quem está agarrado. |
| `sub_0024B348`, `0x24B348` | Procura uma substituição de reversão no banco de movimentos da vítima. |
| `sub_0024B3B8`, `0x24B3B8` | Executa a reversão no frame previsto, troca os papéis e prepara outra resposta. |
| `sub_00248CD8`, `0x248CD8` | Processa os eventos de dano durante o agarrão. |
| `sub_00248ED0`, `0x248ED0` | Trata substituições de movimento relacionadas ao estado dos membros. |
| `sub_00249820` / `sub_00249308` | Caminho de agarrão com um atacante e duas vítimas. |
| `sub_00249E30` / `sub_002498E0` | Caminho de agarrão com dois atacantes e uma vítima. |
| `sub_00255138`, `0x255138` | Inicia um movimento a partir de um frame indicado. |
| `sub_00254A08`, `0x254A08` | Reinicia campos do movimento, incluindo o contador da esquiva. |

O fluxo principal chama `0x24B120` para o tipo de movimento 9 (vítima do agarrão)
e `0x24B3B8` para o tipo 8 (atacante do agarrão).

## Esquiva de golpes e bloqueio depois de errar

Em `0x24D610`, um comando aceito com contador negativo inicializa o contador com
7 ou 9, conforme flags da animação, e o decrementa na mesma chamada. A consulta
`0x24D790` só considera a janela aberta enquanto o valor é maior que zero.
Em atualização normal, isso deixa 6 ou 8 verificações positivas, respectivamente;
as capturas de combo aéreo confirmaram o caminho com 6.

Quando chega a zero, apertar de novo não o reabre: a inicialização exige valor
negativo. `0x254A08` escreve `-1` ao reiniciar o estado do movimento. Uma nova
reação de golpe pode passar por `0x24E880` → `0x255138` → `0x254A08` e liberar
outra tentativa. Outros caminhos de transição também reiniciam esse estado.
Portanto, o tempo fechado depende da transição de movimento, sem duração fixa
confirmada em frames. Flags de animação também podem impedir o processamento.

Na captura local `combate_3.csv`, a janela abriu no frame 26, ficou positiva até
31 e chegou a zero no 32. Comandos nos frames 37, 42 e 57 não a reabriram. O golpe
no frame 69 iniciou outra reação e o contador voltou a `-1`; uma tentativa abriu
outra janela no 71. Os dez golpes dessa captura ocorreram com o contador em zero.

O contador não explica todas as defesas: `0x250480` também consulta estados de
movimento, o campo `+0x9418` e um efeito temporário verificado por
`0x24C328(lutador, 2)`. Uma defesa registrada com contador zero ou negativo não
prova que a janela manual estava aberta.

## Janela de defesa de agarrão

`0x24B120` usa o frame da animação do **atacante**, convertido de `float` para
inteiro. Exige adversários de equipes diferentes, atacante no tipo 8, reversão
cadastrada e frame dentro de um intervalo inclusivo:

```text
primeiro_frame <= int(frame_do_atacante) <= ultimo_frame
```

O início e o fim vêm da tabela do movimento; não são os contadores 7/9 da esquiva
de golpes. Estar dentro desse intervalo ainda exige contador de defesa diferente
de zero e comando/direção aceitos, ou o efeito temporário que permite a defesa.

No caminho normal de `0x24A980`, o contador de defesa da vítima recebe `-1`, que
não é decrementado nem consumido por essa rotina. Há uma exceção: recebe 61 quando
a vítima estava no movimento de defesa indicado pelo campo `+0x0E` de seu banco.
Um contador positivo diminui apenas nas chamadas que passam pelas condições da
janela. Ao receber comando, é zerado **antes** de conferir a direção.

Regras de direção observadas, usando os códigos internos de contexto:

- Contextos 11 e 12: exigem direção retornada por `0x20A028` igual a zero.
- Contextos 13 e 14: não validam a direção nesse trecho.
- Outros contextos: exigem a direção salva no atacante em `+0x9408`, salvo quando
  esse campo é `-1`.

Não foi concluído o mapeamento desses códigos para todos os nomes de agarrão e
direções exibidas no controle. Com contador positivo, uma direção errada perde a
tentativa; com contador `-1`, esse trecho permite tentar novamente dentro da janela.

Uma entrada aceita marca `+0x9428 = 1`. A reversão não precisa ocorrer imediatamente:
`0x24B3B8` espera um frame de execução específico do movimento. O banco da vítima
pode substituir a reversão, o frame de execução e o frame inicial da nova animação.

## A resposta à reversão: “esquiva da esquiva”

Quando `0x24B3B8` executa a reversão, o antigo defensor passa a executar o agarrão e
o antigo atacante recebe a animação de vítima. O antigo atacante fica com:

```text
pedido_de_defesa (+0x9428) = 0
contador_de_defesa (+0x942C) = 4
```

Assim, ele pode responder pelo mesmo `0x24B120`, desde que o novo movimento tenha
uma reversão cadastrada e sua janela esteja válida. O caminho de defesa de golpes
`0x24DF70` também grava contador 4 no antigo atacante quando cria um contra-ataque
com animações em dupla. Evasões individuais seguem outro caminho.

O contador curto é processado assim:

| Verificação elegível | Contador depois do decremento | Pode aceitar entrada? |
|---|---|---|
| 1 | 3 | Sim |
| 2 | 2 | Sim |
| 3 | 1 | Sim |
| 4 | 0 | Não |

Se houver comando numa das três primeiras, ele consome a tentativa naquela chamada.
O contador espera a janela do movimento ficar válida para começar a diminuir.
Com uma chamada elegível por atualização a 60 Hz, as três oportunidades correspondem
a aproximadamente 50 ms. Isso é uma estimativa do caminho de código, ainda sem
captura de agarrão que confirme o tempo efetivo.

A ordem dos slots também importa: cada lutador é processado sequencialmente, de
modo que a troca de papéis pode ser observada pelo próximo slot na mesma atualização
ou somente na seguinte. A execução exige igualdade com o frame configurado; uma
entrada no limite final da janela precisa ser conferida junto dessa ordem.

Uma sequência de reversões só continua se os movimentos seguintes também tiverem
reversões válidas. O contador 4, sozinho, não garante que todo agarrão permita isso.

## Onde estão os tempos nas tabelas

Para um ID de movimento `m`, o índice do registro de agarrão é o `uint16` em
`0x46172A + m * 0x40`. O registro fica em `0x527658 + indice * 0x2A`.

| Offset no registro | Leitor | Significado |
|---|---|---|
| `+0x00` | `0x2156A0` | Movimento da vítima associado ao movimento do atacante. |
| `+0x0A` | `0x215790` | Primeiro frame que aceita defesa. |
| `+0x0C` | `0x2157C8` | Último frame que aceita defesa, inclusive. |
| `+0x0E` | `0x215800` | Movimento de reversão; `0xFFFF` indica ausência. |
| `+0x10` | `0x215838` | Frame em que a reversão é executada. |
| `+0x12` | `0x215870` | Frame inicial da animação de reversão. |

Exemplos lidos diretamente do ELF, com IDs em decimal. São valores básicos da
tabela; não identificam nomes de golpes nem incluem substituições por personagem.

| Movimento | Janela inclusiva | Reversão | Executa no frame | Reversão começa no frame |
|---|---|---|---|---|
| 3112 | 0–30 | 3287 | 30 | 0 |
| 3122 | 0–5 | 3277 | 6 | 0 |
| 3119 | 0–30 | 3119 | 30 | 20 |
| 3758 | 0–60 | 3140 | 75 | 0 |
| 3126 | Sem reversão cadastrada | — | — | — |

O movimento 3119 aponta para si mesmo e reinicia no frame 20: é um exemplo de
dados compatíveis com reversões sucessivas, sujeitos às demais condições acima.
Uma janela 0–30 contém 31 índices de animação; não significa automaticamente 31
atualizações disponíveis, pois o movimento pode começar adiante ou avançar de outra
forma.

As substituições ficam em 16 entradas do banco da vítima: movimento original em
`banco + 0xC2 + i * 8`, reversão em `+0xC4`, execução em `+0xC6` e início em
`+0xC8`. São lidas por `0x213A48`, `0x213A88`, `0x213AC8` e `0x213B08`.
O banco selecionado vem da tabela de ponteiros `0x59B560`, indexada pelo campo
`+0x8D86` do lutador.

## Campos para acompanhar no depurador

Base `B` = ponteiro do lutador na tabela `0x59D168`. Os offsets são relativos a
esse bloco, não ao endereço da vida.

| Campo | Tipo | Uso observado |
|---|---|---|
| `B+0x54` | `float` | Frame atual da animação. |
| `B+0x89C0` | `int16` | Comando de agarrar. |
| `B+0x89C4` | `int16` | Comando de esquiva/defesa consumido por essas rotinas. |
| `B+0x8D40` / `B+0x8D44` | `int32` | ID / tipo do movimento atual. |
| `B+0x93E6` / `B+0x93E8` | `int16` | Movimento do par / slot do adversário no agarrão. |
| `B+0x9404` / `B+0x9408` | `int32` | Contexto do agarrão / direção exigida. |
| `B+0x9418` | `int32` | Estado adicional de encadeamento da defesa de golpes. |
| `B+0x9428` | `int16` | Pedido aceito de defesa de agarrão. |
| `B+0x942C` | `int32` | Contador de defesa de agarrão/reversão. |
| `B+0x9444` | `int16` | Direção registrada para esquiva de golpes. |
| `B+0x9448` / `B+0x944C` | `int32` | Contador / variante da esquiva de golpes. |

## Evidência e limites

Análise estática feita com Ghidra e conferida no código recompilado. Os comentários
de instrução de 22 rotinas centrais (2.816 instruções) foram comparados ao ELF sem
divergências. Os exemplos de tabelas foram lidos do mesmo ELF. As capturas locais
de combo aéreo sustentam o comportamento do contador de esquiva descrito acima.

Ainda falta validar agarrões e contra-reversões em execução, associar os IDs aos
nomes dos golpes e medir a influência de velocidade da animação, pausa e ordem dos
slots. A captura F10 atual registra movimentos e esquivas de golpes, mas não inclui
os campos `+0x9428` e `+0x942C` nem todos os tempos da tabela. Para medir diretamente
essa janela, será necessário acompanhar esses campos no depurador ou ampliar a
captura. Nenhuma regra de combate foi alterada nesta investigação.
