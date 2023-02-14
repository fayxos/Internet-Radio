/**
 * Homepage
 * 
 * - Liste aller Sender
 * - Auswahl eines Senders
 * - Start/Stop des Streams
 * - Anzeige des aktuellen Senders, sowie der Beschreibung des Senders (z.B. Titel und Interpret)
 */
const char homepage[] PROGMEM = R"=====(
<!DOCTYPE html>
  <html>
    <head>
      <meta http-equiv="content-type" content="text/html; charset=UTF-8">
      <meta name="viewport" content="width=device-width, initial-scale=1">
      <link href='http://fonts.googleapis.com/css?family=Lato:400,700' rel='stylesheet' type='text/css'>
      <script src="https://kit.fontawesome.com/8222f9cf59.js" crossorigin="anonymous"></script>
      
      <title>Internet Radio</title>
      
      <style>
        body {
          background-color: #282c31;
          margin: 0;
          font-family: 'Lato', sans-serif;
          font-weight: 400;
        }

        .main_view {
          background-color: #282c31;
          z-index: 2;  
        }

        .sender_view {
          z-index: 1;
        }
        
        .sender {
          text-align:center;
        }
        
        .box {
          background-color: #282c31;
          border-radius: 15px 15px 15px 15px;
          height: 230px;
          width: 200px;
          display: inline-block;
          box-shadow: 1px 12px 18px 1px rgba(0,0,0,0.3);
          margin: 20px;
        }  

        .sender_image {
          border-radius: 10px;
          height: 180px;
          width: 180px;
          display: block;
          margin-left: auto;
          margin-right: auto;
          margin-top: 10px;
          margin-bottom: 5px;
        }

        h1 {
          color: #c6cbda;  
        }

        .sender_title {
          color: #c6cbda;
          white-space: nowrap; 
          text-overflow: ellipsis;
          margin-left: auto;
          margin-right: auto;
          margin-top: auto;
          margin-bottom: auto;  
        }


        .control {
          position: fixed;
          padding: 10px 10px 0px 10px;
          bottom: 0;
          width: 100%;
          height: 90px;
          background-color: #282c31;
          box-shadow: 10px 10px 5px 12px rgba(0,0,0,0.3);
        }

        .current_image {
          border-radius: 10px;
          height: 70px;
          width: 70px;
          display: block;
          margin-left: 20px;
          margin-right: 20px;
          margin-top: 5px;
          margin-bottom: 10px;
        }

        .current_name {
          margin-top: 0px;
          margin-bottom: 0px;
          color: #c6cbda;
        }

        .current_info {
          margin-top: 0px;
          margin-bottom: 0px;
          color: #c6cbda;
        }

        .info {
          float: left;  
        }

        .current_image_main {
          border-radius: 10px;
          height: 70px;
          width: 70px;
          display: block;
          margin-left: 20px;
          margin-right: 20px;
          margin-top: 5px;
          margin-bottom: 10px;
        }

        .pause_resume {
          color: #c6cbda;  
        }

        .actions {
          float: right; 
          margin-top: 5px;
          margin-bottom: 10px;
          margin-right: 50px; 
        }

        .current_name_main {
          
        }

        .current_info_main {
          
        }
      </style>
      <script>
        // Funktion zum Abrufen der Informationen vom Server
        var getJSON = function(url, callback) {
          var xhr = new XMLHttpRequest();
          xhr.open('GET', url, true);
          xhr.responseType = 'json';
          xhr.onload = function() {
            var status = xhr.status;
            if (status === 200) {
              callback(null, xhr.response);
            } else {
              callback(status, xhr.response);
            }
          };
          xhr.send();
        };

        // Ändern des Radiosenders
        function changeStation(name) {
          var url = '/setRadioStation?s=' + name.replace(/\s/g, "+");
          var xhr = new XMLHttpRequest();
          xhr.open('GET', url);
          xhr.send();
        }
        
        // Startem / Stoppen des Streams
        function pauseResume() {
          var url = '/pauseResume';
          var xhr = new XMLHttpRequest();
          xhr.open('GET', url);
          xhr.send();
          getJSON('/getPlayingStatus',
          function(err, data) {
            if (err == null) {
              var info = document.getElementById("play_resume");
              if(info.innerHTML !== data.isPlaying) {
                if(data.isPlaying == true) {
                  info.className = "play_resume fa-solid fa-circle-pause fa-4x";
                } else {
                  info.className = "play_resume fa-solid fa-circle-play fa-4x";
                }
              }
            }
          });
        }

        // Abfragen der Sender und hinzufügen zur Senderliste
        getJSON('/sender',
        function(err, data) {
          if (err == null) {
            var grid = document.getElementById('sender_grid');
            for(var name in data) {
                grid.innerHTML += '<div class="box" onclick="changeStation(\'' + name + '\')">'
                + '<img class="sender_image" src="/' + name.replace(/\s/g, "").toLowerCase() + '" onerror="this.onerror=null;this.src=\'/default\';" />' 
                + '<p class="sender_title">' + name + '</p>' 
                + '</div>';
            }
          }
        });


        // Informationen werden alle 2 Sekunden aktualisiert
        setInterval(function() {
          getData();
        }, 2000); //2000mSeconds update rate
        
        function getData() {

          // Abfragen des aktuellen Radiosenders
          getJSON('/getCurrentRadioStation',
          function(err, data) {
            if (err == null) {
              var name = document.getElementById("current_name");
              if(name.innerHTML !== data.station) {
                name.innerHTML = data.station;

                var image = document.getElementById("current_image")
                image.src = "/" + data.station.replace(/\s/g, "").toLowerCase();
                image.onerror = function() {
                  this.onerror=null;
                  this.src= "/default";
                };
              }
            }
          });

          // Abfragen der aktuellen Info
          getJSON('/getCurrentInfo',
          function(err, data) {
            if (err == null) {
              var info = document.getElementById("current_info");
              if(info.innerHTML !== data.info) {
                info.innerHTML = data.info
              }
            }
          });

          // Abfragen, ob Radio gerade spielt oder nicht
          getJSON('/getPlayingStatus',
          function(err, data) {
            if (err == null) {
              var info = document.getElementById("play_resume");
              if(info.innerHTML !== data.isPlaying) {
                if(data.isPlaying == true) {
                  info.className = "play_resume fa-solid fa-circle-pause fa-4x";
                } else {
                  info.className = "play_resume fa-solid fa-circle-play fa-4x";
                }
              }
            }
          });
        }
      </script>
  </head>
    <body>
    
      
      <div class="sender_view">
        <div class="sender">
          <h1>Sender</h1>
          <div id="sender_grid" class="grid"></div>
        </div>
  
        <div class="control">
          <img id="current_image" class="info current_image" onerror="this.onerror=null;this.src=\'/default\';">
          <div class="info">
            <h1 id="current_name" class="current_name"></h1>
            <h2 id="current_info" class="current_info"></h2>
          </div>

          <div class="actions">
            <i id="play_resume" class="play_resume fa-solid fa-circle-pause fa-4x" onClick="pauseResume()"></i>
          </div>
        </div>
     </div>
      
    </body> 
  </html>
)=====";


/**
 * Connection Page
 * 
 * - Eingabe von SSID und Passwort zum Verbinden zu neuem Netzwerk
 */
const char connectionPage[] PROGMEM = R"=====(
<!DOCTYPE html>
  <html>
    <head>
      <meta http-equiv="content-type" content="text/html; charset=UTF-8">
      <meta name="viewport" content="width=device-width, initial-scale=1">
    
      <title>WIFI Connection</title>

      <style type="css">
        
      </style>
  </head>
    <body>
    
      <p class="validateTips">Enter Information</p>

      // Bei drücken des "Connect" Buttons wird /connect?ssid=<ssid>&password=<password> mit eingegebenen Informationen aufgerufen 
      <form action="/connect">
        <fieldset>
          <label for="ssid">SSID</label>
          <input type="ssid" name="ssid" id="ssid" class="text">
          <label for="password">Password</label>
          <input type="password" name="password" id="password" value="" class="text">
          <input type="submit" value="Connect">
        </fieldset>
      </form>
      
      
    </body> 
  </html>
)=====";
