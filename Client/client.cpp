#include "client.h"
#include "../ipToLatLng/ipToLatLng.h"
#include <cpprest/http_client.h>
#include <iostream>
#include "dirent.h" // for file reading
#include "hash.h"
#include <cstdio> // for printf
#include <stdlib.h>

using namespace std;
using namespace web;
using namespace utility;
using namespace http;
using namespace json;
using namespace web::http;
using namespace web::http::client;


FileInfo newFileInfo(string name, string hash, string cdnAddr) {
  FileInfo f = FileInfo();
  f.name = name;
  f.hash = hash;
  f.cdnAddr = cdnAddr;
  return f;
}

void printFileInfo(FileInfo f) {
  printf("%s %s %s\n", f.name.c_str(), f.hash.c_str(), f.cdnAddr.c_str());
}

Client::Client() {
  baseDir = "./";
}

Client::Client( string orig_ip ) : m_orig_ip(orig_ip) {
  // store ip address of Origin
}

Client::~Client() {
}

void Client::syncDownload() {
  // Get list of files in directory
  vector<FileInfo> files = getListOfFilesFromDirectory();
  for (size_t i = 0; i < files.size(); i++)
    printFileInfo(files[i]);

  // Compare with origin server
  vector<FileInfo> diffFiles = compareListOfFiles(files, 0);

  // For each file that needs to be updated, download
  for(size_t i = 0; i < diffFiles.size();i ++)
    downloadFile(diffFiles[i]);
}

void Client::syncUpload() {
  // Get list of files in directory
  vector<FileInfo> files = getListOfFilesFromDirectory();
  for (size_t i = 0; i < files.size(); i++)
    printFileInfo(files[i]);

  // Compare with origin server
  vector<FileInfo> diffFiles = compareListOfFiles(files, 1);

  // For each file that needs to be updated, upload
  for(size_t i = 0; i < diffFiles.size();i ++)
    uploadFile(diffFiles[i]);
}

vector<FileInfo> Client::compareListOfFiles(vector<FileInfo>& files, int type) {
  // Upload list of file / hashes
  // return list of fileNames that need to be dl'd / uploaded

  /* WHAT WAS NEWLY IMPLEMENTED */
  // create json object to be attached in the http request body
  json::value req_json = json::value::object();
  req_json[U("Type")] = json::value::number(type);

  // create json array of FileList
  json::value req_fileList = json::value::array();
  for (size_t i = 0; i < files.size(); i++) {
    json::value currFileObj = json::value::object();
    currFileObj[U("Name")] = json::value::string(U(files[i].name));
    currFileObj[U("Hash")] = json::value::string(U(files[i].hash));
    currFileObj[U("Address")] = json::value::string(U(files[i].cdnAddr));

    req_fileList[i] = currFileObj;
  }

  // store this array in json object
  req_json[U("FileList")] = req_fileList;

  // Get client ip instance
  ipToLatLng* ip_instance = new ipToLatLng();
  client_ip = ip_instance->getipaddr();

  // use POST http request to retrieve client's latitude/longitude
  ip_instance->IPJsonToLatLng( client_ip );
  client_lat = ip_instance->getlat();
  client_lng = ip_instance->getlng();

  // Now, store these in json object
  req_json[U("IP")] = json::value::string(U(client_ip));
  req_json[U("Lat")] = json::value::number(client_lat);
  req_json[U("Lng")] = json::value::number(client_lng);

  // request message should be directed to Origin IP address
  uri_builder origin_url(U(m_orig_ip));
  origin_url.append_path(U("origin/explicit"));

  http_client client(origin_url.to_uri());	// create client object

  // POST this json message to the origin to ask for which files need to be uploaded/downloaded
  // given the file list already in sync with FSS
  http_response fileComp_resp = client.request( methods::POST ).get();

  vector<FileInfo> diff_files;

  try {
    if (fileComp_resp.status_code() == status_codes::OK ) {
      cout << "compared file list has been retrieved!" << endl;

      json::value jValue = fileComp_resp.extract_json().get();
      json::value& compare_list = jValue.at(U("FileList"));

      
      for(auto& fileObj : compare_list.as_array()) {
        FileInfo fileNew;
        fileNew.name = fileObj.at(U("Name")).as_string();
        fileNew.cdnAddr = fileObj.at(U("Address")).as_string();

	// push the newly constructed file into the list of diff_files
        diff_files.push_back(fileNew);
      }
    }

    else { // handle non-OK status codes

    }
  } catch ( json::json_exception &e ) {
      fprintf(stderr, "JSON object error: %s\n", e.what());
      return diff_files;
  }

  return diff_files;
}

vector<FileInfo> Client::getListOfFilesFromDirectory() {
  cout << "Getting list of files from " << baseDir << endl;
  vector<FileInfo> files;

  DIR *dir = opendir(baseDir.c_str());
  if (dir == NULL) {
    cout << "Could not open directory " << baseDir << endl;
    return files;
  }

  struct dirent *ent;
  while ((ent = readdir(dir)) != NULL) {
    // Skip hidden files
    if (ent->d_name[0] == '.')
      continue;

    // Convert to string object and add to return vector
    FileInfo f = newFileInfo(ent->d_name, hashFile(baseDir+ent->d_name));
    files.push_back(f);
  }

  return files;
}

void Client::downloadFile(FileInfo f) {
  // directly download file from cdn
  printf("Downloading file... ");
  printFileInfo(f);

  // request file f communication
  string cdn_address = "http://" + f.cdnAddr + "/";
  http_client cdn_client = http_client("http://localhost:5000/get");
  
  // Make request
  http_response response;
  try {
    response = cdn_client.request(methods::GET, f.name).get();
  } catch (const std::exception& e) {
    printf("ERROR when downloading, %s\n", e.what());
  }
  
  if (response.status_code() == status_codes::OK) {
    printf("OK, saving...\n");

    string contents = response.extract_string().get();
    cout << contents << endl;

    // Write to file
    ofstream saveFile;
    saveFile.open(baseDir + f.name);
    saveFile << contents;
    saveFile.close();
  } else {
    printf("FAILED TO DOWNLOAD\n");
  }
}

void Client::uploadFile(FileInfo f) {
  printf("Uploading file... ");
  printFileInfo(f);
  
  // Read file body
  ifstream readF(baseDir + f.name);
  std::stringstream buf;
  buf << readF.rdbuf();
  string contents = buf.str();

  // upload file to cdn node
  string cdn_address = "http://" + f.cdnAddr + "/";
  http_client cdn_client = http_client("http://localhost:5000/post");
  
  // Make request
  http_response response;
  try {
    response = cdn_client.request(methods::POST, f.name, contents).get();
  } catch (const std::exception& e) {
    printf("ERROR, %s\n", e.what());
  }
  
  if (response.status_code() == status_codes::OK)
    printf("OK\n");
  else
    printf("FAILED TO UPLOAD\n");
}
