# Diário de Engenharia — Tema 6 (Roteador IP / Patricia + RCU)

Registro das decisões de projeto, bugs e uso de IA, conforme exigido pelo edital
(seção 3). Datas relativas convertidas para absolutas.

## Semana 1 — Fundação e núcleo de ED (E1/E2)

**Decisões**
- IPv4 representado como `uint32_t` em ordem de host com o bit mais significativo
  = bit 0 (esquerda de A.B.C.D). Simplifica `bit_at`, máscaras e LPM.
- Escolha da **Patricia (radix comprimida)** com nós que guardam `key`+`bitlen`
  imutáveis e, separadamente, ponteiros de filho/rota mutáveis. Essa separação
  foi pensada desde já para o RCU: só os campos mutáveis precisam ser atômicos.
- Variante **multibit (stride)** com expansão controlada de prefixos, para a
  comparação experimental. Remoção via *rebuild* a partir da lista autoritativa
  de prefixos — correção trivialmente garantida (expansão torna remoção in-place
  trabalhosa) e remoção não está no caminho quente do benchmark.
- **Oráculo** O(N) força-bruta como verdade-base; toda estrutura é validada por
  teste diferencial randômico contra ele.

**Bugs / aprendizados**
- LPM com compressão de caminho exige *re-verificar* os bits pulados a cada nó
  no `lookup` (`bits_equal`), senão um prefixo mais profundo cujo trecho
  comprimido difere do endereço seria aceito incorretamente.
- Casos do `insert`: (1) prefixo exato, (2) prefixo ancestral de um nó existente
  (splice acima), (3) divergência (nó glue de ramificação), (4) descida. Tratar
  os quatro separadamente eliminou bugs de fronteira (ex.: rota default `/0`).

## Semana 1–2 — Núcleo de SO e robustez (E3/E4)

**Decisões**
- **RCU simplificado (QSBR)**: contador global de épocas; cada leitor publica a
  última época vista em um estado quiescente (entre lookups). O escritor avança a
  época e espera todos os leitores online alcançarem-na antes de liberar nós
  desconectados (free diferido). Ordenação release/acquire na época garante o
  *happens-before* leitor→free, deixando o TSan satisfeito.
- Fachada `router` com dois backends sobre a MESMA Patricia: **RCU** (leitores
  sem trava, escritores serializados por um mutex que os leitores nunca tomam) e
  **rwlock** (baseline). Assim o benchmark compara em cargas idênticas.
- **Gerador programático** da tabela (requisito do enunciado): 10⁵ prefixos
  únicos com histograma de comprimentos ~BGP real (forte em /24), determinístico
  por *seed*, gravados em `data/prefixes.txt` antes dos testes (`make gen`).

**Resultados preliminares** (máquina de desenvolvimento; números finais no
relatório, medidos em Linux):
- Diferencial Patricia × oráculo: 48k+ verificações, 0 falhas.
- Concorrência (fase A): estado final da trie sob 8 leitores + escritor bate
  exatamente com o oráculo.
- Vazão (4 threads, com escritor): RCU ~20 Mlps vs. rwlock ~0,8 Mlps (~26×),
  evidenciando o ganho do plano de dados sem travas.
- Espaço/velocidade: Patricia ~6,5 MB / ~3,4 Mlps; multibit s8 ~305 MB / ~20 Mlps.

## Uso de IA

- Assistente (Claude) usado para estruturar o projeto, redigir o esqueleto dos
  módulos e a bateria de testes diferenciais. Revisão humana focou em:
  - corrigir a re-verificação de bits pulados no LPM;
  - garantir que apenas campos mutáveis dos nós sejam atômicos (desempenho);
  - evitar *deadlock* de `rcu_synchronize` em testes single-thread (reclamar só
    com leitores offline).
- _Registrar aqui, a cada sessão, o prompt usado, o que a IA errou e o que a
  equipe corrigiu._

## Pendências

- [ ] Rodar `make tsan`/`make stress` em Linux e anexar a saída limpa do TSan.
- [ ] `make bench && make graphs` em Linux e inserir os PNGs no relatório.
- [ ] `make valgrind` para confirmar zero vazamentos no encaminhador.
- [ ] Gravar o vídeo de até 5 min (teste de fogo + execução).
