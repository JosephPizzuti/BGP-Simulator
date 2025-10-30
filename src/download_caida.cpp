#include <curl/curl.h>
#include <iostream>
#include <fstream>

size_t write_data(void *ptr, size_t size, size_t nmemb, void *stream) {
  std::ofstream *out = static_cast<std::ofstream*>(stream);
  out->write(static_cast<char*>(ptr), size * nmemb);
  return size * nmemb;
}

int main() {
  CURL *curl = curl_easy_init();
  if (!curl) return 1;

  std::ofstream outfile("20250901.as-rel2.txt.bz2", std::ios::binary);
  curl_easy_setopt(curl, CURLOPT_URL,
      "https://publicdata.caida.org/datasets/as-relationships/serial-2/20250901.as-rel2.txt.bz2");
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &outfile);
  CURLcode res = curl_easy_perform(curl);
  curl_easy_cleanup(curl);
  if (res != CURLE_OK)
    std::cerr << "Download failed: " << curl_easy_strerror(res) << std::endl;
  return (res == CURLE_OK ? 0 : 1);
}
