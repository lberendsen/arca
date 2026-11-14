# ARCA — Sistema de Preservação, Extração Bit a Bit e Reprodução Musical

O **ARCA** é uma aplicação nativa em C++17 para Linux, concebida para a preservação arquivística, extração *bit a bit* comprovada, catalogação e reprodução contínua de CDs de áudio em formato **FLAC de alta resolução**.

Originalmente desenvolvido em C# (.NET/WPF) pelo engenheiro e autor **Lorenzo**, o ARCA foi portado integralmente para C++ nativo sobre **X11, EGL, OpenGL ES 2 e NanoVG**, garantindo desempenho em tempo real, consumo mínimo de memória e independência de ambientes de execução pesados.

---

## 1. Principais Recursos e Diferenciais Técnicos

### 1.1. Extração Física Bit a Bit (SCSI Direto)
* **Comando SCSI Passthrough (`SG_IO`)**: Conversão e envio direto de blocos de comando (CDBs SCSI) para o leitor óptico (`/dev/sr0`), sem interferência de camadas intermediárias de sistema de arquivos.
* **Leitura de Setores com C2 e Subcanal**: Acesso a informações brutas de áudio e metadados de TOC.

### 1.2. Prova Criptográfica e AccurateRip v1 / v2
* **Cálculo de Assinaturas e Checksums**: Implementação limpa dos algoritmos de AccurateRip v1 e v2.
* **Varredura de Deslocamento de Leitura (*Offset Hunting*)**: Varre automaticamente uma janela de **±6.000 amostras** na fonte de dados para localizar o *offset* real de leitura da unidade e validar se o disco extraído corresponde exatamente ao prensado original.
* **Verificação do PCM**: O áudio decodificado do arquivo final é checado via *hash* SHA-256 contra as amostras extraídas do CD físico antes da homologação no acervo.

### 1.3. Codificação FLAC Nativa e Metadados Estendidos
* **Codificação de Referência**: Codificação direta via `libFLAC` em nível de compressão 8 com validação contínua de amostras ativada.
* **Vorbis Comments e Capas Embutidas**: Gravação de metadados padronizados, incluindo ISRC, códigos MCDI, dados AccurateRip e bloco `METADATA_BLOCK_PICTURE` (capa frontal JPEG em alta qualidade).

### 1.4. Cascata Inteligente de Identificação de Álbuns
1. **CD-TEXT**: Leitura das trilhas do disco gravadas no próprio CD óptico (o CD-TEXT tem prioridade estrita nos títulos).
2. **CD Extra**: Extração de dados de mídia integrada.
3. **GnuDB / FreeDB**: Consulta em servidores abertos de discografia.
4. **Discogs API**: Consulta com validação heurística de consistência de durações de faixa (≥ 80% das faixas com tolerância ≤ 10 segundos).
5. **Apple Music**: Consulta alternativa de catálogo e capas em alta resolução.

### 1.5. Digitalização e Tratamento de Encartes (SANE)
* **Integração com Scanner**: Suporte direto ao protocolo SANE para multifuncionais e scanners planos (ex.: Canon Pixma).
* **Estúdio de Imagem Embarcado**: Janelas nativas para digitalização, pré-visualização, recorte de margens (`JanelaCorteImagem`) e divisão de páginas duplas de encartes (`JanelaDivisaoImagem`).

### 1.6. Interface Acelerada por Hardware (NanoVG + Shaders GLSL)
* **Design Âmbar e Escuro**: Fidelidade visual à interface original baseada no tema estilizado do ARCA.
* **Instrumentos Virtuais em Tempo Real**: Osciloscópio estéreo, analisador de espectro FFT e medidores balísticos em dBFS renderizados através de *Compute / Shaders GLSL* em OpenGL ES 2.
* **Eficiência Extrema**: O laço de eventos aguarda interrupções de hardware e eventos do X11; parado, o programa consome 0% de CPU.

---

## 2. Arquitetura do Código-Fonte

O projeto segue rigorosamente o **Princípio da Responsabilidade Única (SRP)** e utiliza **Notação Húngara** nos identificadores para clareza estrita de escopo e tipo de dado:

```
arca/
├── Base/          # Utilidades fundamentais: Arquivo, Despacho, Hash (OpenSSL), Http (CURL), Json, Texto
├── Modelo/        # Regras de negócio: AccurateRip, Acervo, AnalisadorAudio, BancoObras (SQLite), Discogs
├── Interop/       # Camada de baixo nível: SCSI/SG_IO, libFLAC, ALSA, SANE, LeitorAudio (FFmpeg)
├── Vistas/        # Interface gráfica X11/EGL/GLES2/NanoVG: Controles, Diálogos, Janelas, Shaders, Tema
├── Testes/        # Arnês de testes de bancada executáveis contra o hardware físico
├── nanovg/        # Biblioteca de renderização vetorial acelerada 2D (NanoVG GLES2)
├── CMakeLists.txt # Script de compilação multiplataforma CMake
├── compila.sh     # Script autônomo de compilação rápida paralela (sem CMake)
└── xos-arca.cpp   # Ponto de entrada da aplicação
```

---

## 3. Dependências e Requisitos de Compilação

Para compilar o ARCA, são necessários um compilador C++17 moderno (GCC 9+ ou Clang 10+) e as bibliotecas de desenvolvimento de sistema:

### No Debian, Ubuntu ou derivados:
```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential cmake pkg-config \
    libflac-dev libcurl4-openssl-dev libssl-dev libsane-dev \
    libsqlite3-dev libjpeg-dev libasound2-dev \
    libavformat-dev libavcodec-dev libswresample-dev libavutil-dev \
    libx11-dev libegl1-mesa-dev libgles2-mesa-dev zlib1g-dev \
    nlohmann-json3-dev fonts-noto-core
```

### No Alpine Linux:
```bash
apk add build-base cmake pkgconf \
    flac-dev curl-dev openssl-dev sane-dev \
    sqlite-dev libjpeg-turbo-dev alsa-lib-dev \
    ffmpeg-dev libx11-dev mesa-dev \
    zlib-dev nlohmann-json font-noto
```

### No Fedora:
```bash
sudo dnf install -y \
    gcc-c++ cmake pkgconfig \
    flac-devel libcurl-devel openssl-devel sane-backends-devel \
    sqlite-devel libjpeg-turbo-devel alsa-lib-devel \
    ffmpeg-free-devel libX11-devel mesa-libEGL-devel mesa-libGLES-devel \
    zlib-devel json-devel google-noto-sans-fonts
```

---

## 4. Como Compilar

### Opção A: Usando CMake (Recomendado)
```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```
O executável principal será gerado em `build/arca`.

### Opção B: Compilação Rápida Autônoma (Script Shell)
```bash
chmod +x compila.sh
./compila.sh
```
Os binários serão gerados no diretório `./bin/`.

---

## 5. Como Executar

Para iniciar a aplicação gráfica sob qualquer servidor de janelas X11:
```bash
./bin/arca
```

### Executando Testes de Bancada:
Os testes unitários e de hardware podem ser executados individualmente:
* `./bin/teste-cd`: Testa a presença de mídia e leitura de TOC SCSI no `/dev/sr0`.
* `./bin/teste-flac`: Testa a integridade do codificador e decodificador FLAC.
* `./bin/teste-scanner`: Testa a detecção e calibração do scanner via SANE.
* `./bin/teste-identifica`: Testa a resolução em cascata de identificação de álbuns.

---

## 6. Autoria e Licença

* **Autor**: Lorenzo
* **Projeto Original**: ARCA (C# / .NET / WPF)
* **Porte Nativo**: C++17 / X11 / EGL / GLES2 / NanoVG
* **Licença**: MIT (ou conforme especificado pelo autor).
