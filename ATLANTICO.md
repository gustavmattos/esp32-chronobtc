# Atlântico — relógio e clima para CYD

O firmware atual está em `src/main.cpp`; o sketch `RELOGIOESP.ino` permanece como referência histórica. O build PlatformIO compila apenas `src/`.

## Interface

- Paisagem, 320 × 240, ILI9341 na CYD clássica.
- Relógio 24h sem segundos, data, BTC/USD e USD/BRL: 10 segundos.
- Florianópolis (latitude -27.5954, longitude -48.5480), clima atual, hora pequena e três dias **a partir de amanhã**: 20 segundos.
- Cada dia mostra ícone desenhado, máxima/mínima em Celsius e probabilidade máxima de precipitação.
- Deslizamento horizontal de 350 ms com aceleração e desaceleração; volta em sentido oposto. Tempo de permanência começa ao terminar a transição.
- Dois sprites RGB332 (153.600 bytes no total), tarefa de rede separada, último valor válido preservado e aviso para dados pendentes/desatualizados.
- Sem DHT22 obrigatório; temperatura externa vem da previsão meteorológica.

## Fontes dos dados

- [Open-Meteo](https://open-meteo.com/en/docs): clima a cada 10 minutos; fuso America/Sao_Paulo. Dados meteorológicos fornecidos por Open-Meteo, sob CC BY 4.0.
- [Coinbase](https://docs.cdp.coinbase.com/coinbase-app/track-apis/prices): BTC/USD spot, a cada 2 minutos. USD real, em vez do par BTC/USDT antigo.
- [AwesomeAPI](https://docs.awesomeapi.com.br/api-de-moedas): USD/BRL bid, a cada 2 minutos.
- [Frankfurter](https://frankfurter.dev/): alternativa diária USD/BRL quando a principal falha. A tela identifica `USD/BRL DIA`; não é uma cotação intradiária.

HTTPS valida os certificados com raízes selecionadas do pacote ca-certificates. Hora obtida por SNTP, com fuso BRT3 (UTC−3). A rede não bloqueia a interface. Sem internet, a hora continua após a sincronização; não há RTC independente para manter a hora após desligar.

## Configurar e gravar

1. Copie `include/secrets.h.example` para `include/secrets.h` e preencha o Wi-Fi. O arquivo é ignorado pelo Git. A senha fica incorporada no binário local: não publique esse binário.
2. Compile com `pio run`.
3. Conecte a CYD por USB e grave com `pio run -t upload --upload-port PORTA`.
4. Abra o monitor com `pio device monitor --port PORTA --baud 115200`.

A partição configurada é min_spiffs; a primeira gravação deve ser por USB, incluindo a tabela de partições. O arquivo `firmware.bin` sozinho não é uma imagem completa para flash vazio. Este firmware não implementa OTA.

## Validação

Build PlatformIO CYD validado. Consultas reais de Open-Meteo, Coinbase e Frankfurter validadas no ambiente de desenvolvimento; AwesomeAPI retornou HTTP 429, motivando a alternativa diária. O ambiente de desenvolvimento não está conectado ao Wi-Fi do usuário: a conexão nessa rede só poderá ser verificada na placa.

Pendentes de hardware: pinagem/variante exata da CYD, heap durante TLS com sprites alocados, legibilidade a 1 metro, cor, fluidez da transição, reconexão Wi-Fi e consultas HTTPS no ESP32. A prévia em `design/atlantico.svg` é um estudo de composição com dados ilustrativos; a tipografia real do firmware usa as fontes internas da TFT_eSPI.
