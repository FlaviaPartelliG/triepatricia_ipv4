# EXECUÇÃO — Tema 6 (Roteador IP por LPM: Patricia + RCU)

Guia de como compilar e executar o projeto, com o papel de cada ponto de entrada.

Ambiente alvo: **WSL Ubuntu / Linux** com `gcc`, `make`, `pthreads`,
ThreadSanitizer, Valgrind e (para gráficos) `python3` + `matplotlib`.

```sh
sudo apt update
sudo apt install -y build-essential valgrind python3-matplotlib
```

A partir da pasta do projeto (no WSL: `/mnt/c/Users/Ferri/Projects/Tema 6`).

---

## 1. Caminho rápido (ordem recomendada)

```sh
make gen      # 1) gera data/prefixes.txt com 10^5 prefixos (já fica pronto)
make all      # 2) compila as 4 ferramentas, sem warnings
make test     # 3) corretude: testes unitários + concorrência (ASan + UBSan)
make tsan     # 4) teste de fogo concorrente sob ThreadSanitizer (prova de no-races)
make bench    # 5) coleta dados de vazão (CSVs em bench/)
make graphs   # 6) gera os PNGs do relatório a partir dos CSVs
```

`make help` lista todos os alvos.

---

## 2. Alvos do Makefile (entrypoints de build/execução)

| Alvo | O que faz |
|------|-----------|
| `make gen` | Roda `bin/gen_prefixes` e escreve `data/prefixes.txt` (10⁵ prefixos BGP-like, determinístico). Use `GEN_N=...` para mudar a quantidade. |
| `make all` | Compila as 4 ferramentas em `bin/` com `-Wall -Wextra -Werror -O2 -pthread`. |
| `make test` (=`make asan`) | Compila e roda os 5 testes sob **AddressSanitizer + UBSan** (corretude + ausência de vazamentos/UAF). |
| `make tsan` | Compila e roda o **teste de fogo** (`test_concurrent`) sob **ThreadSanitizer**: 10⁵ prefixos, 8 leitores + 1 escritor. Não pode acusar data races. |
| `make stress` | Igual ao `tsan`, porém mais longo/pesado (mais churn e duração). |
| `make bench` | Roda `bin/bench` (RCU×rwlock, 1/2/4/8/16 threads, com e sem escritor) e `bin/bench_struct`, gravando CSVs em `bench/`. |
| `make graphs` | Executa `scripts/plot.py` para gerar `bench/*.png` a partir dos CSVs. |
| `make valgrind` | Roda o encaminhador sob Valgrind (`--leak-check=full`) para confirmar zero vazamentos. |
| `make clean` | Remove `bin/` e saídas de `bench/` (mantém `data/prefixes.txt`). |

Script equivalente ao fluxo experimental completo: `bash scripts/run_bench.sh`.

---

## 3. Ferramentas (binários em `bin/`)

Todas são geradas por `make all`. As que consultam a tabela exigem `make gen` antes.

### `bin/gen_prefixes` — gerador da tabela
Cria programaticamente a tabela de roteamento (requisito do tema: a tabela já
fica pronta antes da execução).

```sh
bin/gen_prefixes [arquivo] [quantidade] [seed] [n_ifaces]
# padrão: data/prefixes.txt 100000 1 256
```
Saída: arquivo texto com linhas `A.B.C.D/len  iface`.

### `bin/router_sim` — simulador / encaminhador (demo principal)
Carrega a tabela, encaminha um fluxo de pacotes sintéticos por LPM e reporta a
interface de saída de cada um. É a demonstração do plano de dados.

```sh
bin/router_sim [--table PATH] [--packets N] [--backend rcu|rwlock] \
               [--threads T] [--hit R] [--show K]
# ex.: bin/router_sim --table data/prefixes.txt --packets 2000000 --threads 8 --show 8
```
- `--backend` escolhe RCU (leitura sem trava) ou rwlock (baseline).
- `--threads` divide o fluxo entre T threads de encaminhamento.
- `--hit` fração de pacotes mirados em prefixos existentes (0..1).
- `--show K` imprime as K primeiras decisões de encaminhamento.
Reporta total/hits/misses e vazão em Mpps.

### `bin/bench` — benchmark de concorrência (RCU × rwlock)
Mede a **vazão de lookups/s** variando o número de threads leitoras, com os dois
backends, opcionalmente com um escritor concorrente. Gera os dados do gráfico de
escalabilidade do relatório.

```sh
bin/bench [--table PATH] [--threads "1,2,4,8,16"] [--seconds S] \
          [--writer on|off] [--out CSV] [--hit R]
```
Saída CSV: `backend,threads,writer,lookups_per_sec,mlookups_per_sec`.

### `bin/bench_struct` — comparação de estruturas (Patricia × multibit)
Mede vazão de lookup (1 thread) e memória da Patricia vs. tries multibit de
strides 4 e 8. Gera os dados do gráfico de troca espaço×velocidade.

```sh
bin/bench_struct [--table PATH] [--packets N] [--out CSV]
```
Saída CSV: `structure,stride,prefixes,nodes,mem_kb,mlookups_per_sec`.

---

## 4. Testes (entrypoints de verificação)

Construídos e executados pelos alvos `make test` (ASan+UBSan) e `make tsan`
(ThreadSanitizer). Para rodar manualmente, compile cada um linkando `src/*.c`.

| Teste | Verifica |
|-------|----------|
| `test_ipv4` | parsing/formatação de IP e prefixos, máscaras, bits, common-prefix. |
| `test_patricia` | LPM/inserção/remoção da Patricia via teste diferencial vs. oráculo. |
| `test_multibit` | trie multibit (strides 1/2/4/8) vs. oráculo. |
| `test_rcu` | mecânica do RCU/QSBR (defer/synchronize/reclaim) + corretude do router (RCU e rwlock). |
| `test_concurrent` | **teste de fogo**: leitores concorrentes + escritor. Fase A compara estado final com o oráculo; Fase B sustenta 10⁵ prefixos com 8 threads. Alvo do TSan. |

Argumentos do teste de fogo (sobrescrevem os padrões):
```sh
test_concurrent [n0] [readers] [churn_ops] [scale_n0] [scale_ms]
# n0       = prefixos-base da Fase A (corretude)
# readers  = nº de threads leitoras
# churn_ops= operações de inserção/remoção do escritor na Fase A
# scale_n0 = prefixos da Fase B (teste de fogo em escala; use 100000)
# scale_ms = duração da Fase B em milissegundos
```

---

## 5. Resumo dos artefatos gerados

| Caminho | Origem | Conteúdo |
|---------|--------|----------|
| `data/prefixes.txt` | `make gen` | tabela de 10⁵ prefixos |
| `bin/*` | `make all` | ferramentas e testes compilados |
| `bench/*.csv` | `make bench` | dados de vazão e de estruturas |
| `bench/*.png` | `make graphs` | figuras do relatório |
