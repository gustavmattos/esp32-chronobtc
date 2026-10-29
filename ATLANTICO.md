# Atlântico — relógio e clima para CYD

O firmware atual está em `src/main.cpp`; o sketch `RELOGIOESP.ino` permanece como referência histórica. O build PlatformIO compila apenas `src/`.

## Interface

- Paisagem, 320 × 240, ILI9341 na CYD clássica.
- Tela principal inspirada no relógio anterior: fundo preto, hora 24h grande com segundos em amarelo, data branca, BTC/USD em verde e USD/BRL em laranja. Permanece 10 segundos.
- Segunda tela: Florianópolis (latitude -27.5954, longitude -48.5480), temperatura externa, clima atual, hora pequena e três dias **a partir de amanhã**. Permanece 10 segundos.
- Cada dia mostra ícone desenhado, máxima/mínima em Celsius e probabilidade máxima de precipitação.
- Deslizamento horizontal de 350 ms com aceleração e desaceleração; volta em sentido oposto. Tempo de permanência começa ao terminar a transição.
- Dois sprites de 4 bits com paleta de 8 cores (76.800 bytes no total), para deixar memória livre para HTTPS e OTA. Tarefa de rede separada, último valor válido preservado e aviso para dados pendentes/desatualizados.
- Sem DHT22 obrigatório; temperatura externa vem da previsão meteorológica.

## Fontes dos dados

- [Open-Meteo](https://open-meteo.com/en/docs): clima a cada 10 minutos; fuso America/Sao_Paulo. Dados meteorológicos fornecidos por Open-Meteo, sob CC BY 4.0.
- [Coinbase](https://docs.cdp.coinbase.com/coinbase-app/track-apis/prices): BTC/USD spot, a cada 2 minutos. USD real, em vez do par BTC/USDT antigo.
- [AwesomeAPI](https://docs.awesomeapi.com.br/api-de-moedas): USD/BRL bid, a cada 2 minutos.
- [Frankfurter](https://frankfurter.dev/): alternativa diária USD/BRL quando a principal falha. A tela identifica `USD/BRL DIA`; não é uma cotação intradiária.

HTTPS valida os certificados com raízes selecionadas do pacote ca-certificates. Hora obtida por SNTP, com fuso BRT3 (UTC−3). A rede não bloqueia a interface. Sem internet, a hora continua após a sincronização; não há RTC independente para manter a hora após desligar.

## Configurar e gravar

1. Copie `include/secrets.h.example` para `include/secrets.h` e preencha o Wi-Fi e uma senha forte para OTA. O arquivo é ignorado pelo Git. As senhas ficam incorporadas no binário local: não publique esse binário.
2. Compile com `pio run`.
3. Conecte a CYD por USB e grave com `pio run -t upload --upload-port PORTA`.
4. Abra o monitor com `pio device monitor --port PORTA --baud 115200`.
5. Para as próximas gravações pela rede local, use `./scripts/upload_ota.ps1` no PowerShell. O IP configurado atualmente é `192.168.3.219`; se o roteador atribuir outro IP, use `./scripts/upload_ota.ps1 -IpAddress IP_DA_PLACA`. A senha OTA é lida de `include/secrets.h` e não precisa ser digitada no comando. O IP atual aparece no monitor serial.

A partição configurada é min_spiffs, com dois slots de aplicativo para OTA. A primeira gravação deve ser por USB, incluindo a tabela de partições. O arquivo `firmware.bin` sozinho não é uma imagem completa para flash vazio. Depois disso, o firmware recebe atualizações OTA autenticadas enquanto está conectado ao Wi-Fi.

## Validação

Build e flash USB validados na CYD conectada à COM3. Wi-Fi `APTO 4` conectado, dados de mercado e clima carregados na placa, e upload OTA autenticado concluído com reinicialização e retorno do serviço em `192.168.3.219:3232`. A mudança posterior que remove a temperatura da primeira tela e deixa ambas em 10 segundos compilou, mas ainda não foi gravada na placa.

Pendente de avaliação visual: legibilidade a 1 metro, cores, proporções e preferência dos moradores pelo novo layout. A prévia em `design/atlantico.svg` representa a proposta anterior e não corresponde mais ao firmware atual.
