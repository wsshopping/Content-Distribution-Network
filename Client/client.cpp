#include "client.h"
#include "../ipToLatLng/ipToLatLng.h"
#include <cpprest/http_client.h>
#include <iostream>
#include "dirent.h" // for file reading
#include "hash.h"
#include <cstdio> // for printf
#include <stdlib.h>
#include <sys/stat.h> // for dir checking
#include <sys/types.h>
#include <unistd.h>

using namespace std;
using namespace web;
using namespace utility;
using namespace http;
using namespace json;
using namespace web::http;
using namespace web::http::client;


FileInfo newFileInfo(string name, string hash, string timestamp, string cdnAddr) {
  FileInfo f = FileInfo();
  f.name = name;
  f.hash = hash;
  f.timestamp = timestamp;
  f.cdnAddr = cdnAddr;
  return f;
}

void printFileInfo(FileInfo f) {
  printf("%s %s %s\n", f.name.c_str(), f.hash.c_str(), f.cdnAddr.c_str());
}

Client::Client() {
  baseDir = "./";

  // get the ip_address of the client and lat/lng
  // Get client ip instance
  ip_instance = new ipToLatLng();
  client_ip = ip_instance->getipaddr();

  // use GET http request to retrieve client's latitude/longitude
  ip_instance->IPJsonToLatLng( client_ip );
  client_lat = ip_instance->getlat();
  client_lng = ip_instance->getlng();
}

Client::Client( string orig_ip ) : m_orig_ip(orig_ip) {
  // store ip address of Origin

  baseDir = "./";

  // get the ip_address of the client and lat/lng
  // Get client ip instance
  ip_instance = new ipToLatLng();
  client_ip = ip_instance->getipaddr();

  // use GET http request to retrieve client's latitude/longitude
  ip_instance->IPJsonToLatLng( client_ip );
  client_lat = ip_instance->getlat();
  client_lng = ip_instance->getlng();
}

Client::~Client() {
  // delete the allocated ip_instance
  delete ip_instance;
}

void Client::syncDownload() {
  // Get list of files in directory
  vector<FileInfo> files = getListOfFilesFromDirectory("");
  for (size_t i = 0; i < files.size(); i++)
    printFileInfo(files[i]);
  cout << endl;

  // Compare with origin server
  vector<FileInfo> diffFiles = compareListOfFiles_explicit(files, 1);

  // For each file that needs to be updated, download
  for(size_t i = 0; i < diffFiles.size();i ++) {
    printFileInfo(diffFiles[i]);
    downloadFile(diffFiles[i]);
  }
}

void Client::syncUpload() {
  // Get list of files in directory
  vector<FileInfo> files = getListOfFilesFromDirectory("");
  for (size_t i = 0; i < files.size(); i++)
    printFileInfo(files[i]);
  cout << endl;

  // Compare with origin server
  vector<FileInfo> diffFiles = compareListOfFiles_explicit(files, 0);

  // For each file that needs to be updated, upload
  for(size_t i = 0; i < diffFiles.size();i ++)
    uploadFile(diffFiles[i]);
}

void Client::compareListOfFiles_sync(vector<FileInfo>& files) {

  // create json object to be attached in the http request body
  json::value req_json = json::value::object();

  // create json array of FileList
  json::value req_fileList = json::value::array();
  for (size_t i = 0; i < files.size(); i++) {
    json::value currFileObj = json::value::object();
    currFileObj[U("Name")] = json::value::string(U(files[i].name));
    currFileObj[U("Hash")] = json::value::string(U(files[i].hash));
    currFileObj[U("TimeStamp")] = json::value::string(U(files[i].timestamp));

    req_fileList[i] = currFileObj;
  }

  // store this array in json object
  // and also the IP, Lat, Lng
  req_json[U("FileList")] = req_fileList;
  req_json[U("IP")] = json::value::string(U(client_ip));
  req_json[U("Lat")] = json::value::number(client_lat);
  req_json[U("Lng")] = json::value::number(client_lng);

  // request message should be directed to Origin IP address
  uri_builder origin_url(U(m_orig_ip));
  http_client client(origin_url.to_uri());  // create client object

  // POST this json message to the origin to ask for which files need to be uploaded/downloaded
  // given the file list already in sync with FSS
  http_response fileComp_resp;
  try {
    fileComp_resp = client.request( methods::POST, U("/origin/sync/"), req_json ).get();
  } catch (const std::exception& e) {
    cout << "ERROR: compare list of files => sync : " << e.what() << endl;
  }

  // process the file list that is returned in response
  // distinguish whether it's download/upload
  try {
    if (fileComp_resp.status_code() == status_codes::OK ) {
      cout << "compared file list has been retrieved! SYNC" << endl;

      json::value jValue = fileComp_resp.extract_json().get();
      json::value& compare_list = jValue.at(U("FileList"));

      
      for(auto& fileObj : compare_list.as_array()) {
        FileInfo fileNew;
        fileNew.name = fileObj.at(U("Name")).as_string();
        fileNew.cdnAddr = fileObj.at(U("Address")).as_string();

        // push the newly constructed file into the list of either downloadFileList or uploadFileList
        if (fileObj.at(U("Type")).as_string() == "UP") {
          uploadFileList.push_back(fileNew);
        } else if(fileObj.at(U("Type")).as_string() == "DOWN") {
          downloadFileList.push_back(fileNew);
        } else {
          fprintf(stderr, "Wrong file type has been detected!\n");
        }
        
      }
    } else { // handle non-OK status codes
        fprintf(stderr, "Response from Origin failed :(\n");
          return;
    }
  } catch ( json::json_exception &e ) {
      fprintf(stderr, "JSON object error: %s\n", e.what());
      return;
  }

  return;
}

vector<FileInfo> Client::compareListOfFiles_explicit(vector<FileInfo>& files, int type) {

  // create json object to be attached in the http request body
  json::value req_json = json::value::object();
  req_json[U("Type")] = json::value::number(type);

  // create json array of FileList
  json::value req_fileList = json::value::array();
  for (size_t i = 0; i < files.size(); i++) {
    json::value currFileObj = json::value::object();
    currFileObj[U("Name")] = json::value::string(U(files[i].name));
    currFileObj[U("Hash")] = json::value::string(U(files[i].hash));
    //currFileObj[U("Address")] = json::value::string(U(files[i].cdnAddr));

    req_fileList[i] = currFileObj;
  }

  // store this array in json object
  // and also the IP, Lat, Lng
  req_json[U("FileList")] = req_fileList;
  req_json[U("IP")] = json::value::string(U(client_ip));
  req_json[U("Lat")] = json::value::number(client_lat);
  req_json[U("Lng")] = json::value::number(client_lng);

  // request message should be directed to Origin IP address
  uri_builder origin_url(U(m_orig_ip));
  http_client client(origin_url.to_uri());	// create client object

  // POST this json message to the origin to ask for which files need to be uploaded/downloaded
  // given the file list already in sync with FSS
  http_response fileComp_resp;
  try {
    fileComp_resp = client.request( methods::POST, U("/origin/explicit/"), req_json ).get();
  } catch (const std::exception& e) {
    cout << "ERROR: compare list of files => explicit : " << e.what() << endl;
  }

  vector<FileInfo> diff_files;

  try {
    if (fileComp_resp.status_code() == status_codes::OK ) {
      cout << "compared file list has been retrieved! EXPLICIT" << endl;

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
        fprintf(stderr, "Response from Origin failed :(\n");
    }
  } catch ( json::json_exception &e ) {
      fprintf(stderr, "JSON object error: %s\n", e.what());
      return diff_files;
  }

  return diff_files;
}

bool Client::isDir(string dirPath) {
  // Return 1 if dir, else 0
  struct stat s;
  stat(dirPath.c_str(), &s);

  if (s.st_mode & S_IFDIR)
    return 1;
  else
    return 0;
}

vector<FileInfo> Client::getListOfFilesFromDirectory(string subpath) {
  /*
   * Recursively find all files in basedir/subpath
   * for the top level call, use subpath="", basedir will be appeneded automatically
   */
  string path = baseDir + subpath;
  cout << "Getting list of files from " << path << endl;
  vector<FileInfo> files;

  DIR *dir = opendir(path.c_str());
  if (dir == NULL) {
    cout << "Could not open directory " << path << endl;
    return files;
  }

  struct dirent *ent;
  struct stat st;

  while ((ent = readdir(dir)) != NULL) {
    // Skip hidden files
    if (ent->d_name[0] == '.')
      continue;

    if (isDir(path+ent->d_name)) {
      cout << path + ent->d_name << " is dir" <<endl;
      vector<FileInfo> subDirFiles = getListOfFilesFromDirectory(string(ent->d_name) + "/");
      files.insert(files.end(), subDirFiles.begin(), subDirFiles.end());
      continue;
    }

    // convert file's path from string to char * array
    string str_path = subpath+ent->d_name;
    char filePath[1024];
    strncpy(filePath, str_path.c_str(), sizeof(filePath));
    filePath[sizeof(filePath) - 1] = 0;

    // get the file statistics
    int ierr = stat(filePath, &st);
    if (ierr < 0)
      fprintf(stderr, "File statistics was not able to be found!\n");

    // get the last modified timestamp in char string
    unsigned long lval = st.st_mtime;
    char timebuf[50];
    sprintf(timebuf, "%lu" , lval);
    string timestamp_str = timebuf;

    //cout << "File = " << filePath << " timestamp: " << timestamp_str << endl;

    // Convert to string object and add to return vector
    FileInfo f = newFileInfo(subpath+ent->d_name, hashFile(path+ent->d_name), timestamp_str);
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
  http_client cdn_client = http_client(cdn_address + "cdn/cache/");
  
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
    cout << response.to_string() << endl;
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
  http_client cdn_client = http_client(cdn_address + "cdn/cache/");
  
  // Make request
  http_response response;
  try {
    if (f.hash == "")
      f.hash = hashFile(baseDir + f.name);

    response = cdn_client.request(methods::PUT, f.name + "?" + f.hash, contents).get();
  } catch (const std::exception& e) {
    printf("ERROR, %s\n", e.what());
  }
  
  if (response.status_code() == status_codes::OK)
    printf("OK\n");
  else {
    printf("FAILED TO UPLOAD\n");
    cout << response.to_string() << endl;
  }
}
