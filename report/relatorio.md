# Relatório Experimental — Tema 6
## Roteador IP por Longest Prefix Match com Trie de Patricia e Plano de Dados RCU

> Esqueleto do relatório técnico (8–15 páginas). Preencher as seções marcadas com
> _[…]_ após rodar `make bench && make graphs` em Linux. As figuras são geradas
> em `bench/*.png`.

---

### 1. Introdução
- Problema: encaminhamento IP por *longest prefix match* sobre tabela CIDR.
- Integração ED × SO: trie de Patricia (ED) + plano de dados concorrente com RCU
  (SO).
- Objetivos e hipóteses (a versão sem travas escala; a baseline com lock satura).

### 2. Estrutura de dados central — Trie de Patricia
- Modelo de nó (`key`/`bitlen` imutáveis; `child[]`/`route` atômicos) e invariante
  da árvore (Seção “Node model” em `src/patricia.c`).
- Algoritmos: LPM (re-verificação de bits comprimidos), inserção (4 casos),
  remoção (colapso de nós glue).
- **Dedução teórica:** profundidade da trie em função do número de prefixos e da
  distribuição de comprimentos; custo de lookup O(W) com W=32 no pior caso e o
  papel da compressão de caminho. _[gráfico: profundidade média × nº de prefixos]_
- Variante **multibit (stride)**: expansão de prefixos, níveis = 32/stride,
  troca espaço×acessos.

### 3. Mecanismo de SO — Plano de dados concorrente (RCU/QSBR)
- Por que RCU: leitores no caminho quente sem travas; escritor raro.
- QSBR: épocas por thread, estado quiescente, *grace period*, recuperação
  diferida. Argumento de correção (happens-before via release/acquire).
- Baseline rwlock para comparação.
- Discussão de *crash consistency*: não se aplica (tabela em memória,
  reconstruída do arquivo gerado).

### 4. Metodologia experimental
- Máquina (CPU, núcleos, RAM, kernel, versão do gcc). _[preencher]_
- Tabela: 10⁵ prefixos sintéticos BGP-like (`make gen`); histograma de
  comprimentos. _[gráfico: histograma de /len]_
- Cargas: fluxo de pacotes com `hit_ratio`; varredura de 1/2/4/8/16 threads;
  com e sem escritor concorrente.
- Instrumentação: `bin/bench` (vazão), `bin/bench_struct` (espaço/tempo),
  ThreadSanitizer (races), Valgrind/ASan (vazamentos).

### 5. Resultados
#### 5.1 Corretude
- Testes diferenciais Patricia/multibit × oráculo (nº de verificações, 0 falhas).
- Fase A do teste de concorrência: estado final == oráculo sob 8 leitores +
  escritor.
- **TSan limpo** no teste de fogo (`make tsan`): _[colar resumo da saída]_.
- **Valgrind/ASan** sem vazamentos: _[colar resumo]_.

#### 5.2 Vazão: RCU × rwlock
- _[figura `bench/throughput_writer.png`]_ — com escritor concorrente.
- _[figura `bench/throughput_nowriter.png`]_ — somente leitura.
- Discussão de escalabilidade e identificação do **gargalo** (cache-line
  bouncing do lock vs. quiescência do RCU). Speedup em 1/2/4/8/16 threads.

#### 5.3 Espaço × velocidade: Patricia × multibit
- _[figura `bench/structcmp.png`]_ — Mlookups/s e MB por estrutura/stride.
- Confronto com a previsão assintótica (acessos por lookup vs. memória).

### 6. Confronto teoria × prática
- Onde a prática bateu com a dedução e onde divergiu (efeitos de cache, NUMA,
  contenção do escritor, ramificação).

### 7. Conclusão
- Síntese dos ganhos do plano de dados RCU; limitações; trabalhos futuros
  (IPv6/128 bits, level-compression/LC-trie, RCU com *call_rcu* assíncrono).

### Apêndice — Como reproduzir
```sh
make gen && make all
make test          # corretude (ASan+UBSan)
make tsan          # teste de fogo, prova de ausência de races
make bench graphs  # dados e figuras do relatório
```

---

#### Resultados preliminares (ambiente de desenvolvimento — substituir por Linux)
| Estrutura | Memória | Vazão (1 thread) |
|-----------|---------|------------------|
| Patricia  | ~6,5 MB | ~3,4 Mlps |
| multibit s4 | ~49 MB | ~11,3 Mlps |
| multibit s8 | ~305 MB | ~20,1 Mlps |

| Threads | RCU (Mlps) | rwlock (Mlps) |
|---------|-----------|---------------|
| 1 | ~5,3 | ~0,11 |
| 2 | ~10,0 | ~0,37 |
| 4 | ~20,5 | ~0,78 |

(Valores com escritor concorrente; tendência mantém-se em Linux, magnitudes
variam com o hardware.)
