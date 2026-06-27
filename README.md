# Tema 6 — Roteador IP por Longest Prefix Match (Trie de Patricia)

Trabalho Interdisciplinar — Estrutura de Dados (C) × Sistemas Operacionais
(IFES Cachoeiro de Itapemirim).

Um simulador de tabela de roteamento IPv4 que faz **Longest Prefix Match (LPM)**
sobre uma **trie de Patricia** (árvore radix comprimida) e o consulta a partir de
um **plano de dados concorrente**: múltiplas threads fazem *lookups* sem travas
enquanto uma thread de controle insere/remove rotas, usando um **RCU
simplificado (QSBR)** com recuperação diferida de memória. Uma variante baseada
em **rwlock** serve de baseline para comparação de vazão.

## Componentes

| Camada | Arquivos | Papel |
|--------|----------|-------|
| Primitivas IPv4 | `include/ipv4.h`, `src/ipv4.c` | endereços, prefixos CIDR, máscaras, bits |
| ED central | `include/patricia.h`, `src/patricia.c` | trie de Patricia, LPM, inserção/remoção com (des)compressão de caminho |
| ED comparativa | `include/multibit.h`, `src/multibit.c` | trie multibit (stride) com expansão de prefixos |
| Oráculo | `include/lpm_oracle.h`, `src/lpm_oracle.c` | referência O(N) força-bruta para os testes |
| SO central | `include/rcu.h`, `src/rcu.c` | RCU/QSBR: epochs por thread, grace period, free diferido |
| Fachada | `include/router.h`, `src/router.c` | backends RCU (sem trava) e rwlock |
| Gerador | `include/prefixgen.h`, `src/prefixgen.c` | tabela BGP-like de 10⁵ prefixos, determinística |
| Pacotes | `include/pktgen.h`, `src/pktgen.c` | fluxo sintético de destinos |
| Ferramentas | `tools/` | `gen_prefixes`, `router_sim`, `bench`, `bench_struct` |
| Testes | `tests/` | unitários + diferencial vs. oráculo + concorrência (TSan) |

## Requisitos (WSL / Linux)

```sh
sudo apt update
sudo apt install -y build-essential valgrind python3-matplotlib
```

`build-essential` traz `gcc` e os runtimes dos sanitizers (`libasan`, `libtsan`,
`libubsan`). `python3-matplotlib` é só para os gráficos.

## Uso

```sh
make gen          # gera data/prefixes.txt com 10^5 prefixos (já fica pronto)
make all          # compila as ferramentas, sem warnings (-Wall -Wextra -Werror)
make test         # testes unitários + concorrência sob AddressSanitizer + UBSan
make tsan         # teste de fogo concorrente (10^5 prefixos, 8 threads) sob TSan
make stress       # versão mais longa/pesada sob TSan
make bench        # gera os CSVs de vazão em bench/
make graphs       # renderiza os PNGs a partir dos CSVs
make valgrind     # checagem de vazamentos no encaminhador
make help         # lista os alvos
```

Simulador de encaminhamento:

```sh
bin/router_sim --table data/prefixes.txt --packets 2000000 --threads 8 --backend rcu --show 8
```

Benchmark RCU × rwlock:

```sh
bin/bench --table data/prefixes.txt --threads "1,2,4,8,16" --seconds 3 --writer on
```

## Teste de fogo (anti-atalho)

`make tsan` executa o teste exigido pelo tema: carrega 10⁵ prefixos e sustenta
*lookups* concorrentes de 8 threads enquanto rotas são inseridas/removidas,
provando com o ThreadSanitizer a **ausência de data races**. O `bench` mede a
**vazão de lookups/s da versão RCU vs. a versão protegida por lock**.

## Persistência / recuperação

Este tema **não** é transacional em disco: a tabela é mantida em memória e
reconstruída a partir de `data/prefixes.txt` (artefato gerado) a cada execução.
Não há, portanto, requisito de sobreviver a `kill -9` com estado em disco — o
foco de SO aqui é o plano de dados concorrente (RCU), não *crash consistency*.

## Mapa das entregas

- **E1 — Fundação:** `ipv4`, nós/estrutura da Patricia, oráculo, testes-base.
- **E2 — Núcleo de ED:** LPM/inserção/remoção com compressão, variante multibit.
- **E3 — Núcleo de SO:** RCU/QSBR sem travas + baseline rwlock, sem races (TSan).
- **E4 — Robustez e relatório:** teste de fogo, vazão RCU×rwlock, escalabilidade
  1/2/4/8/16 threads, relatório experimental e vídeo.
