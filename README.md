# cefbrowser 
Works together with [vdr-plugin-web](https://github.com/Zabrimus/vdr-plugin-web) and [remotetranscode](https://github.com/Zabrimus/remotetranscode) to show HbbTV application and stream videos.

### Dependencies (cross compile, Ubuntu 22.04)
#### arm
```
apt install -y zlib1g-dev:armhf libssl-dev:armhf libcrypt-dev:armhf libglib2.0-dev:armhf libnss3-dev:armhf \
        libatk1.0-dev:armhf libatk-bridge2.0-dev:armhf libcups2-dev:armhf libxcomposite-dev:armhf libxdamage-dev:armhf \
        libxrandr-dev:armhf libgbm-dev:armhf libxkbcommon-dev:armhf libpango1.0-dev:armhf libasound2-dev:armhf
```

#### arm64
```
sudo apt install -y zlib1g-dev:arm64 libssl-dev:arm64 libcrypt-dev:arm64 libglib2.0-dev:arm64 libnss3-dev:arm64 \
        libatk1.0-dev:arm64 libatk-bridge2.0-dev:arm64 libcups2-dev:arm64 libxcomposite-dev:arm64 libxdamage-dev:arm64 \
        libxrandr-dev:arm64 libgbm-dev:arm64 libxkbcommon-dev:arm64 libpango1.0-dev:arm64 libasound2-dev:arm64
```

### Build (default is x86)
```
./setup.sh <arm, arm64 or x86>
meson setup build
cd build
meson compile
meson install
```
The whole binary including all necessary files/libs exists in folder ```build/Release```.

### Configuration
A default configuration can be found in folder config: ```sockets.ini```.

:fire: All ports/ip addresses in ```sockets.ini``` must be the same as for ```vdr-plugin-web``` and ```remotetranscoder```.
It's safe to use the same sockets.ini for all of the three parts (vdr-plugin-web, cefbrowser, remotetranscoder). 

### Start
Change to directory ```build/Release```.

```./cefbrowser  --config=/path/to/sockets.ini --remote-allow-origins=http://localhost:9222```

or if you don't need the possibility to debug the application in Chrome

```./cefbrowser  --config=/path/to/sockets.ini```

### Parameters
cefbrowser knows parameters which exists exclusively for the browser. 
Apart from these parameter a huge list of other parameters exists. 
Nearly of parameters of Chromium can be used. See e.g. [here](https://peter.sh/experiments/chromium-command-line-switches/). 
It's up to you to decide what you want to use and test ;)

```
-c / --config </path/to/sockets.ini>   (mandatory parameter)
-l / --loglevel <level> (from 0 to 4, where 4 means very, very verbose logging)
-z / --zoom <value> Zooms the http page to the desired width. 
         The result often looks much better than the default value 1280.
         The downside is, that more data needs to be processed or send.
         Valid values are: 1280, 1920, 2560 and 3840
-q / --osdqoi The images between the browser and VDR are compressed using 
         [qoi](https://qoiformat.org/) and are transferred via TCP/IP over 
         the network. With this option the browser can be started on a 
         different machine as VDR.                      
-f / --fullosd Normally only changed parts of the OSD are exchanged between 
         the browser and VDR. With this option the full OSD will exchanged. 
         This is much slower but could lead to visual better results.
-p / --profilePath <path> Defines the path where cefbrowser shall 
         save his profile data
-s / --staticPath <path> Mandatory static content can be found in 
         this directory. E.g. css, html, javascript and so on.
-b / --bindall The browser uses every network interface. Could lead to 
         interesting effects if Docker/Incus/LXD is also installed.                  
-r / --remotedebug <port> Enables the Chromium DevTools on the given
         port (e.g. 9222). Disabled by default.
```

### Logging
Log entries will be written to stdout/stderr.

### Tested channels
- Das Erste
- ZDF

#### Tested installation of cefbrowser in VDR*ELEC/CE-20/21
Install at first the precompiled cef binaries:
```install.sh -c <URL>```
Where correct <URL> can be found at https://github.com/Zabrimus/VDRSternELEC/releases. 
Choose one of:
```
cef-126.2.7-aarch64.zip  
cef-126.2.7-arm.zip      
cef-126.2.7-x86_64.zip
```   
The choice depends on your system.
Copy the zip to ```/storage/.update``` and call ```/usr/local/bin/install.sh -c``` 

Start the script ```/usr/local/bin/install.sh -w```
This script installs all necessary files.

Adapt the configuration file ```/storage/.config/vdropt/sockets.ini``` accordingly. The default configuration uses in all cases localhost.
  ```
  [vdr]
  http_ip = 127.0.0.1
  http_port = 50000

  [browser]
  http_ip = 127.0.0.1
  http_port = 50001

  [transcoder]
  http_ip = 127.0.0.1
  http_port = 50002
  ```
 
- Optional: hbbtv sqllite3 database

   For Vodafone West users a prefilled database exists in ```/storage/cefbrowser/data/database```.
   ```
   cp /storage/browser/cefbrowser/database/Vodafone_West_hbbtv_urls.db /storage/browser/cefbrowser/database/hbbtv_urls.db
   ```
   Everony else needs to copy an already existing database or have to prefill the database. 
   A channel switch to the desired channel and wait some minutes is sufficient.

reboot

- Choose a channel in VDR and goto Menu/Web. Wait or press directly the red button.