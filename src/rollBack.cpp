#include <Arduino.h>
#include <HTTPClient.h>

// command line on pi 
//sudo curl http://localhost/post-esp-data.php -d "api_key=xxxxxx&sensor=BME&locstion....."

int deleteRow(String phpScript);
String performHttpGet(const char *url);


int deleteRow(String phpScript)
{
   String payload = performHttpGet(phpScript.c_str());
       
    return 1;

}



