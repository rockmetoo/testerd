#include <stdio.h>
#include <string.h>
 
#include <curl/curl.h>
 
int main(int argc, char *argv[])
{
  CURL *curl;
  CURLcode res;
 
  struct curl_httppost *formpost=NULL;
  struct curl_httppost *lastptr=NULL;
  struct curl_slist *headerlist=NULL;
  static const char buf[] = "Expect:";
 
  curl_global_init(CURL_GLOBAL_ALL);
 
  /* Fill in the file upload field */ 
  curl_formadd(&formpost,
               &lastptr,
               CURLFORM_COPYNAME, "file",
               CURLFORM_FILE, "/home/tanvir/shogun.zip",
               CURLFORM_END);
 

 
  curl = curl_easy_init();
  /* initalize custom header list (stating that Expect: 100-continue is not
     wanted */ 
  headerlist = curl_slist_append(headerlist, buf);
  if(curl) {
    /* what URL that receives this POST */ 
    curl_easy_setopt(curl, CURLOPT_URL, "http://localhost/fileUpload.php");
    if ( (argc == 2) && (!strcmp(argv[1], "noexpectheader")) )
      /* only disable 100-continue header if explicitly requested */ 
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerlist);
    curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);
 
    /* Perform the request, res will get the return code */ 
    res = curl_easy_perform(curl);
	
	double		dVal;
	
    /* Check for errors */ 
    if(res != CURLE_OK)
      printf("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
	else
	{
		curl_easy_getinfo(curl, CURLINFO_SPEED_UPLOAD, &dVal);
		
		printf("Upload Speed: %.3f\n", dVal/1024);
	}
 
    /* always cleanup */ 
    curl_easy_cleanup(curl);
 
    /* then cleanup the formpost chain */ 
    curl_formfree(formpost);
    /* free slist */ 
    curl_slist_free_all (headerlist);
  }
  return 0;
}
