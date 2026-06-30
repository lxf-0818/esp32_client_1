#include <Arduino.h>
#include <HTTPClient.h>

// command line on pi 
//sudo curl http://localhost/post-esp-data.php -d "api_key=xxxxxx&sensor=BME&locstion....."
//pi@raspberrypi:/var/www/html $ curl -G --data-urlencode "key=1" "http://192.168.1.9/parse.php"
//1|1
//BME680 

int deleteRow(String phpScript);
String performHttpGet(const char *url);


int deleteRow(String phpScript)
{
   String payload = performHttpGet(phpScript.c_str());
   Serial.printf("delete payload %s]n",payload.c_str());

   return 1;

}



