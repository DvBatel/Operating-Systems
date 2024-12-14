#include <unistd.h>
#include <string.h>
#include <vector>
#include <sstream>
#include <sys/wait.h>
#include <iomanip>
#include "Commands.h"
#include <iostream>
#include <cerrno>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <regex>
#include <algorithm>
#include <dirent.h>
#include <sys/syscall.h> 
#include <sys/socket.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <netinet/in.h>

using namespace std;

const std::string WHITESPACE = " \n\r\t\f\v";

#if 0
#define FUNC_ENTRY()  \
  cout << __PRETTY_FUNCTION__ << " --> " << endl;

#define FUNC_EXIT()  \
  cout << __PRETTY_FUNCTION__ << " <-- " << endl;
#else
#define FUNC_ENTRY()
#define FUNC_EXIT()
#endif

/**
* Finds the first non-whitespace character in the string and returns a substring starting from that character to the end.
*/
string _ltrim(const std::string &s) {
    size_t start = s.find_first_not_of(WHITESPACE);
    return (start == std::string::npos) ? "" : s.substr(start);
}

/**
* Finds the last non-whitespace character in the string and returns a substring from the start to that character (inclusive).
*/
string _rtrim(const std::string &s) {
    size_t end = s.find_last_not_of(WHITESPACE);
    return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

/**
* Uses _ltrim and _rtrim to remove whitespace at the start and end of the string, respectively.
* @return std::string - new string with both leading and trailing whitespace removed.
*/
string _trim(const std::string &s) {
    return _rtrim(_ltrim(s));
}


/**
* Parses a command line string into individual arguments and stores pointers to 
* these arguments in the provided args array.
* @return int The number of arguments parsed.
*
* @note The function trims leading/trailing whitespace before parsing.
*       Memory for each argument is allocated dynamically and must be freed using _argsFree.
*/
int _parseCommandLine(const char *cmd_line, char **args) {
    FUNC_ENTRY()
    int i = 0;
    std::istringstream line_steam(_trim(string(cmd_line)).c_str());
    for (std::string s; line_steam >> s;) {
        args[i] = (char *) malloc(s.length() + 1);
        memset(args[i], 0, s.length() + 1);
        strcpy(args[i], s.c_str());
        args[++i] = NULL;
    }
    return i;

    FUNC_EXIT()
}

/**
* Iterates over the arguments array and frees the dynamically allocated memory for each argument.
*/
void _argsFree(int args_count, char **args) {
    if (!args)
        return;

    for (int i = 0; i < args_count; i++) {
        free(args[i]);
    }
}

/** 
* @return true if it is a background command
*/ 
bool _isBackgroundComamnd(const char *cmd_line) {
    const string str(cmd_line);
    return str[str.find_last_not_of(WHITESPACE)] == '&';
}

/**
* Remove & from cmd_line 
*/
void _removeBackgroundSign(char *cmd_line) {
    const string str(cmd_line);
    // find last character other than spaces
    unsigned int idx = str.find_last_not_of(WHITESPACE);
    // if all characters are spaces then return
    if (idx == string::npos) {
        return;
    }
    // if the command line does not end with & then return
    if (cmd_line[idx] != '&') {
        return;
    }
    // replace the & (background sign) with space and then remove all tailing spaces.
    cmd_line[idx] = ' ';
    // truncate the command line string up to the last non-space character
    cmd_line[str.find_last_not_of(WHITESPACE, idx) + 1] = 0;
}

/**
* iterates over the arguments to determine if the command involves output redirection using the '>' or '>>' symbols.
*/
bool isRedirection(int numOfArgs, char** args) {                                
  for (int i = 0; i < numOfArgs; i++) {
    if (!strcmp(args[i], ">") || !strcmp(args[i], ">>")) {
      return true;
    }
  }
  return false;
}

/**
* Iterates over the arguments to determine if the command involves piping using the '|' or '|&' symbols.
*/
bool isPipe(int numOfArgs, char** args) {                                 
  for (int i = 0; i < numOfArgs; i++) {
    if (!strcmp(args[i], "|") || !strcmp(args[i], "|&")) {
      return true;
    }
  }
  return false;
}

/**
* verifies if the input string contains only numeric characters and is not empty.
*/
bool _isPositiveInteger(const std::string& str) {
    if (str.empty()) return false;

    for (char c : str) {
        if (!isdigit(c)) {
            return false;
        }
    }
    return true;
}

/**
* verifies that the string starts with '-' followed by a numeric signal number. It also ensures the number is in the valid range [1, 32].
*/
bool  _isValidSignal(const std::string& str) {
  if (str.empty() || str.length() == 1) {
    return false;
  }
  if (str[0] != '-') {
    return false;
  }
  for (size_t i = 1; i < str.length(); ++i) {
    if (!isdigit(str[i])) {
      return false;  // If a non-digit character is found, return false
    }
  }
  return true;
}

/**
* extracts the first argument from cmd_line
*/
string _getFirstArg(const char* cmd_line) {
  string cmd_s = _trim(string(cmd_line));
  return cmd_s.substr(0, cmd_s.find_first_of(" \n"));
}

/**
* searches for the given command (arg) in all directories listed in the PATH environment variable and checks if it is executable.
*/
bool isInPATHEnvVar(const char* arg) {
  char *pathEnv = getenv("PATH");
  if (!pathEnv) {
    perror("smash error: getenv failed");
    return false;
  }

  std::istringstream pathStream(pathEnv);
  std::string pathDir;

  while (std::getline(pathStream, pathDir, ':')) {
    
    std::string full_path = pathDir + "/" + arg;    // construct the full path to the command
    if (access(full_path.c_str(), X_OK) == 0) {     // check if the file exists and is executable
      return true;
    }
  }
  return false;
}

/**
* check if the argument starts with "./" (simple exe)
*/
string isSimpleInDirectoryExe(const char* arg) {
  string ptr = arg;
  ptr = _trim(ptr);
  if (ptr.rfind("./", 0) == 0) {
    return ptr.substr(2);
  }
  return "";
}

/**
* searches for the given command (arg) in all the current directory and its sub directories recurssivly
*/
std::string findExecutableInCurrentDir(const std::string& dirPath, const std::string& target) {
    int fd = open(dirPath.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd == -1) {
        perror(("Failed to open directory: " + dirPath).c_str());
        return "";
    }

    char buffer[BUF_SIZE];
    struct linux_dirent* d;
    int nread;

    while ((nread = syscall(SYS_getdents, fd, buffer, BUF_SIZE)) > 0) {
        for (int bpos = 0; bpos < nread;) {
            d = (struct linux_dirent*) (buffer + bpos);
            std::string entryName = d->d_name;

            // Skip "." and ".."
            if (entryName == "." || entryName == "..") {
                bpos += d->d_reclen;
                continue;
            }

            // Construct the full path of the entry
            std::string fullPath = dirPath + "/" + entryName;

            // Check if the entry matches the target
            if (entryName == target) {
                close(fd);
                return fullPath;  // Found the executable file
            }

            // Check if the entry is a directory
            struct stat st;
            if (fstatat(fd, entryName.c_str(), &st, AT_SYMLINK_NOFOLLOW) == 0 && S_ISDIR(st.st_mode)) {
                // Recursively search in subdirectories
                std::string result = findExecutableInCurrentDir(fullPath, target);
                if (!result.empty()) {
                    close(fd);  // Close the current directory before returning
                    return result;  // Return the found path
                }
            }

            bpos += d->d_reclen;
        }
    }

    if (nread == -1) {
        perror(("Failed to read directory entries in: " + dirPath).c_str());
    }

    close(fd);
    return "";  // Return an empty string if not found
}

/**
* Iterates through the arguments and checks for the presence of wildcard characters, which are commonly used in shell commands.
*/
bool containsWildcards(char** args) {
  for (int i = 1; i < COMMAND_MAX_ARGS; ++i) {
    if (!args[i]) return false;
    else if (args[i][0] == '*' || args[i][0] == '?') { 
      return true;
    }
  }
  return false;  // No wildcards found
}

/**-----------------------------Command---------------------------------*/
// C'tor:
Command::Command(const char *cmd_line){
  this->cmd_line = cmd_line;
}

std::string Command::getLine(){
    return this->cmd_line;
}

/**--------------------------------------------------------------------*/
/**---------------------------BuiltInCommands--------------------------*/
/**--------------------------------------------------------------------*/


/**---------------------------BuiltInCommand---------------------------*/
// C'tor:
BuiltInCommand::BuiltInCommand(const char *cmd_line) : Command(cmd_line) {
  this->cmd_line = cmd_line;
}

/**---------------------------CHPromptCommand--------------------------*/
// C'tor:
CHPromptCommand::CHPromptCommand(const char *cmd_line) : BuiltInCommand(cmd_line) {
  char* args[COMMAND_MAX_ARGS];
  string line = cmd_line;
  int numOfArgs = _parseCommandLine(line.c_str(), args);    // parse the command line
  if (!numOfArgs) return;

  this->newPromptName =  args[1] ? args[1]: "";
  _argsFree(numOfArgs, args);
}

void CHPromptCommand::execute() {
  SmallShell::getInstance().changePrompt(this->newPromptName);
}

/**---------------------------GetCurrDirCommand--------------------------*/
// C'tor:
GetCurrDirCommand::GetCurrDirCommand(const char *cmd_line) : BuiltInCommand(cmd_line) {
  this->cmd_line = cmd_line;
}

void GetCurrDirCommand::execute() {
  std::cout << SmallShell::getInstance().getCurrentDirectory() << std::endl;
}

/**---------------------------JobsCommand--------------------------------*/
// C'tor:
JobsCommand::JobsCommand(const char *cmd_line, JobsList *jobs) : BuiltInCommand(cmd_line) {
  this->cmd_line = cmd_line;
  this->jobs = jobs;
}

void JobsCommand::execute() {
  this->jobs->printJobsList();
}

/**---------------------------FGCommand----------------------------------*/
//C'tor:
FGCommand::FGCommand(const char *cmd_line) : BuiltInCommand(cmd_line) {
  this->cmd_line = cmd_line;
}

void FGCommand::execute() {
  int id;
  char* args[COMMAND_MAX_ARGS];
  string line = cmd_line;
  int numOfArgs = _parseCommandLine(line.c_str(), args);    // parse the command line
  JobsList::JobEntry* job;
  SmallShell &smash = SmallShell::getInstance();

  // 1 - fg , 2 - num , 3 - invalid
  if(numOfArgs > 2 ){
    std::cerr << "smash error: fg: invalid arguments" << std::endl;
    _argsFree(numOfArgs, args);
    return;
  }
  if(numOfArgs == 2){
    string job_id = args[1];

    // fg [job-id]
    if (_isPositiveInteger(job_id)){
      id = stoi(job_id);
      job = smash.getJobs()->getJobById(id);
      if (!job){
        std::cerr << "smash error: fg: job-id " << id << " does not exist" << std::endl;
        _argsFree(numOfArgs, args);
        return;
      }
    }
    else{
      std::cerr << "smash error: fg: invalid arguments" << std::endl;
      _argsFree(numOfArgs, args);
      return;
    }
  }
  _argsFree(numOfArgs, args);

  // fg
  if (numOfArgs == 1){
    job = smash.getJobs()->getLastJob(&id);
    if (!job){
      std::cerr << "smash error: fg: jobs list is empty" << std::endl;
      return;
    }
  }
  // print the found job
  std::cout << job->getCommandLine() << " " << job->getPID() << std::endl;
  job->isActive = false;
  smash.setFGPID(job->getPID());
  waitpid(job->getPID(), NULL, 0);
  smash.getJobs()->removeJobById(id);
  smash.setFGPID(INITIAL_FG);
}



/**----------------------------QuitCommand---------------------------------*/
// C'tor:
QuitCommand::QuitCommand(const char *cmd_line, JobsList *jobs) : BuiltInCommand(cmd_line) {
  this->cmd_line = cmd_line;
  this->jobs = jobs;
}

void QuitCommand::execute() {
  this->jobs->removeFinishedJobs();

  char* args[COMMAND_MAX_ARGS];
  string line = cmd_line;
  int numOfArgs = _parseCommandLine(line.c_str(), args);    // parse the command line
  if (!numOfArgs) return;

  string secondArg = args[1] ? args[1] : "";
  _argsFree(numOfArgs, args);
  
  if (!secondArg.compare("kill")) {
    std::cout << "smash: sending SIGKILL signal to " << jobs->jobs.size() << " jobs:" << std::endl;

    for (auto it = jobs->jobs.begin(); it != jobs->jobs.end();) {
      std::cout << it->getPID() << ": " << it->getCommandLine() << std::endl;
      if (kill(it->getPID(), SIGKILL) == -1) {
        perror("smash error: kill failed");
        continue;
      }
      it->isActive = false;
      it = jobs->jobs.erase(it);
    }
  }
  exit(0);
}

/**----------------------------KillCommand---------------------------------*/
// C'tor:
KillCommand::KillCommand(const char *cmd_line, JobsList *jobs) : BuiltInCommand(cmd_line) {
  this->cmd_line = cmd_line;
  this->jobs = jobs;
}

// void KillCommand::execute() {

//   char* args[COMMAND_MAX_ARGS];
//   string line = cmd_line;
//   int numOfArgs = _parseCommandLine(line.c_str(), args);    // parse the command line
//   if (!numOfArgs) return;

//   string secondArg = args[1] ? args[1] : "";
//   string thirdArg = args[2] ? args[2] : "";
//   _argsFree(numOfArgs, args);
  
//   //only 1-2 args or not valid numbers or not a valid signal
//   if ((!secondArg.compare("") || !thirdArg.compare("")) &&
//       ( _isValidSignal(secondArg) || !_isPositiveInteger(thirdArg))) {
//         std::cerr << "smash error: kill: invalid arguments" << std::endl;
//     return;
//   }
//   int ID = stoi(thirdArg);
//   JobsList::JobEntry* job = jobs->getJobById(ID);

//   if (!job) {
//     std::cerr << "smash error: kill: job-id " << ID << " does not exist" << std::endl;
//     return;
//   }
//   //need to kill job when adding the function
//   int signal = abs(stoi(secondArg));
//   std::cout << "signal number " << signal <<" was sent to pid " << job->getPID() << std::endl;
//   kill(job->getPID(), signal);

//   if (signal == SIGKILL) {
//     job->isActive = false;
//   }

//   jobs->removeFinishedJobs();
// }



void KillCommand::execute() {
  
  char* args[COMMAND_MAX_ARGS];
  string line = cmd_line;
  int numOfArgs = _parseCommandLine(line.c_str(), args);    // parse the command line
  // 1 - kill 2- signum 3- jobid

  // validate format
  if (numOfArgs != 3 ){
    std::cerr << "smash error: kill: invalid arguments" << std::endl;
    _argsFree(numOfArgs, args);
    return;
  }
  // valid signum and positive job-id
  else if(!_isValidSignal(args[1]) || !_isPositiveInteger(args[2])){
    std::cerr << "smash error: kill: invalid arguments" << std::endl;
    _argsFree(numOfArgs, args);
    return;
  }

  int job_id = stoi(args[2]);
  int signal = abs(stoi(args[1]));
  JobsList::JobEntry* job = jobs->getJobById(job_id);

  if (!job) { // job doesnt exist
    std::cerr << "smash error: kill: job-id " << job_id << " does not exist" << std::endl;
    _argsFree(numOfArgs, args);
    return;
  }
  //else, job exists
  if(kill(job->getPID(), signal) == -1){ //if failed to kill the job
    perror("smash error: kill failed");
    _argsFree(numOfArgs, args);
    return;
  }
  std::cout << "signal number " << signal <<" was sent to pid " << job->getPID() << std::endl;

  jobs->removeFinishedJobs();           //if did not fail
  
  _argsFree(numOfArgs, args);
  return;
}

/**----------------------------ShowPidCommand----------------------------------*/
// C'tor:
ShowPidCommand::ShowPidCommand(const char *cmd_line) : BuiltInCommand(cmd_line) {
  this->pid = getpid();
}

void ShowPidCommand::execute() {
  std::cout << "smash pid is " << this->pid << std::endl;
}

/**---------------------------ChangeDirCommand---------------------------------*/
// C'tor:
ChangeDirCommand::ChangeDirCommand(const char *cmd_line, char **plastPwd) : BuiltInCommand(cmd_line){
  //this->last_pwd = plastPwd;
  
  char* args[COMMAND_MAX_ARGS];
  string line = cmd_line;
  int numOfArgs = _parseCommandLine(line.c_str(), args);    // parse the command line
  if (!numOfArgs) return;

  this->dir_to_repl =  args[1] ? args[1] : "";
  _argsFree(numOfArgs, args);

}

void ChangeDirCommand::execute(){
  char* args[COMMAND_MAX_ARGS];
  int argCount = _parseCommandLine(this->dir_to_repl.c_str(), args);
  SmallShell &smash = SmallShell::getInstance();
  string cwd = smash.getCurrentDirectory();

  // no dir, do nothing
  if (argCount == 0){
    _argsFree(argCount, args);
    return;
  }
  // max args allowed is 1:
  else if (argCount > 1){
    std::cerr << "smash error: cd: too many arguments" << std::endl;
    return;
  }
  // go back to previous dir
  else if (strcmp(args[0] ,"-") == 0){
    // if dir wasn't changed before
    if (strcmp(smash.lastPwd.c_str() ,"-1") == 0){
      std::cerr << "smash error: cd: OLDPWD not set" << std::endl;
      return;
    }
    // change to prev dir
    else{
      if(chdir(smash.lastPwd.c_str()) != 0){
        std::perror("smash error: chdir failed");
        return;
      }
      else{
        smash.lastPwd = cwd;
        smash.setCurrentDirectory(getcwd(nullptr, 0));
      }
    }
  }
  // change to new dir and update prev dir to current
  else{
      if(chdir(args[0]) != 0){
        std::perror("smash error: chdir failed");
      }
      else{
        smash.lastPwd = cwd;
        smash.setCurrentDirectory(getcwd(nullptr, 0));
      }
  }
  _argsFree(argCount, args);
  return;
}

/**---------------------------aliasCommand---------------------------------*/
// C'tor:    aliasCommand(const char *cmd_line, std::map<std::string, std::string>& alias_map, std::set<std::string>& built_in_commands, std::list<std::pair<std::string, std::string>>& alias_list);

aliasCommand::aliasCommand(const char *cmd_line, std::map<std::string, std::string>& alias_map,  std::set<std::string>& built_in_commands, std::list<std::pair<std::string, std::string>>& alias_list): BuiltInCommand(cmd_line),
                                                                                                                                        cmd_line(cmd_line),
                                                                                                                                        alias_map(alias_map),
                                                                                                                                        built_in_commands(built_in_commands),
                                                                                                                                        alias_list(alias_list)                                                                                       
{

}

void aliasCommand::execute(){
  char* args[COMMAND_MAX_ARGS+1];
  int argCount = _parseCommandLine(this->cmd_line.c_str(), args);

  bool temp = true;
  // only alias was in cmd -> print all of aliased commands
  if (argCount == 1){
    // show content:
    for (const auto& cur : alias_list){
      std::cout << cur.first << "='" << cur.second << "'" << std::endl;
    }
    temp = false;
  }

  string clean_command = _trim(this->cmd_line);
  // in substr: first argument is where to start and 2nd argument how many chars to read
  std::string name = clean_command.substr(clean_command.find_first_of(" ") + 1, clean_command.find_first_of("=") - clean_command.find_first_of(" ") -1);
  std::string command = clean_command.substr(clean_command.find_first_of("=") + 1 );
  while(temp == true) {

    if(clean_command.find_first_of("=") == string::npos){
      std::cerr << "smash error: alias: invalid syntax =" << std::endl;
      temp = false;
      continue;
    }

    // remove the first and last ' '
    if (!command.empty() && command.front() == '\'') {
        command.erase(command.begin());
    }
    else {
      std::cerr << "smash error: alias: invalid syntax" << clean_command << std::endl;
      temp = false;
      continue;
    }
    if (!command.empty() && command.back() == '\'') {
        command.pop_back();
    }
    else{
      std::cerr << "smash error: alias: invalid syntax" << clean_command << std::endl;
      temp = false;
      continue;
    }

    auto aliasCommand = this->alias_map.find(name);
    auto builtInCommand = this->built_in_commands.find(command);

    // command already exists -> error
    if (aliasCommand != this->alias_map.end() || builtInCommand != this->built_in_commands.end()){
        std::cerr << "smash error: alias: "<< name <<" already exists or is a reserved command" << std::endl;
    }
    // try to add to map
    // validate the format and syntax
    else if (std::regex_match(name, std::regex("^[a-zA-Z0-9_]+$"))){
      this->alias_list.push_back({name, command});
      this->alias_map[name] = command;
    }
    else{
      std::cerr << "smash error: alias: invalid syntax" <<std::endl;
    }
    temp = false;
    continue;
  }
  _argsFree(argCount, args);
}

/**---------------------------unaliasCommand---------------------------------*/
// C'tor:
unaliasCommand::unaliasCommand(const char *cmd_line, std::map<std::string, std::string>& alias_map, std::list<std::pair<std::string, std::string>>& alias_list): BuiltInCommand(cmd_line),alias_map(alias_map), alias_list(alias_list){
  this->cmd_line = _trim(string(cmd_line));
}

void unaliasCommand::execute(){
  // format the cmd-line
  char* args[COMMAND_MAX_ARGS+1];
  int argCount = _parseCommandLine(this->cmd_line.c_str(), args);

  if (argCount == 1){
    std::cerr << "smash error: unalias: not enough arguments" << std::endl;
  }
  for( int i = 1; i<argCount; i++){
    // the name found
    if(this->alias_map.find(args[i]) != this->alias_map.end()){
      std::string name = args[i];
      // Remove the alias from the list
      for (auto it = this->alias_list.begin(); it != this->alias_list.end(); ++it) {
        if (it->first == name) {
          this->alias_list.erase(it);
          break;
        }
      }
      this->alias_map.erase(name);
    }
    else{
      std::cerr << "smash error: unalias: "<< args[i] <<" alias does not exist" << std::endl;
      break;
    }
  }
  _argsFree(argCount, args);
}

/**---------------------------aliasConverterFunction---------------------------------*/
// returns converted cmd in case of aliased command. if not aliased - does nothing.
string replaceAliased(const char *cmd_line, const map<std::string, std::string> map ){
    // format the cmd-line
  char* args[COMMAND_MAX_ARGS + 1];
  int argCount = _parseCommandLine(cmd_line, args);

  if (argCount > 0) {
    auto aliasCommand = map.find(args[0]);
    // it is an alias command
    if (aliasCommand != map.end()){
      std::string replacedCommand = aliasCommand->second;
      // append remaining arguments from cmd_line 
      for (int i = 1; i < argCount; ++i) {
          replacedCommand += " ";           
          replacedCommand += args[i];
      }
      _argsFree(argCount, args);
      return replacedCommand;
    }
  }
  // clean up and return original cmd_line
  _argsFree(argCount, args);
  return string(cmd_line);
}

/**---------------------------NetInfoCommand---------------------------------*/
// C'tor:
NetInfo::NetInfo(const char *cmd_line) : Command(cmd_line), cmd_line(cmd_line){
}

void NetInfo::execute() {
  char *args[COMMAND_MAX_ARGS];
  int numOfArgs = _parseCommandLine(cmd_line.c_str(), args);
  std::string interface;

  if (numOfArgs < 2){
    std::cerr << "smash error: netinfo: interface not specified" << std::endl;
    _argsFree(numOfArgs, args);
    return;
  // TODO: what to do if more then one interface?
  } 
  else{
    interface = args[1];
  }
  _argsFree(numOfArgs, args);

  if(!isInterfaceValid(interface)){
    return;
  }

  printIPSubnet(interface);
  printDeafultGateway(interface);
  printDNSServers();

  return;
}

void NetInfo::printDeafultGateway(const std::string &interface){
  char buffer[BUF];

  int fd = open("/proc/net/route", O_RDONLY);
  if (fd == -1) {
    perror("smash error: open failed");
    return;
  }

  ssize_t b_read = read(fd, buffer, BUF - 1);
  if (b_read <= 0) {
    perror("smash error: read failed");
    close(fd);
    return;
  }

  buffer[b_read] = '\0';

  std::istringstream stream(buffer);
  std::string line;

  // reads each line from the string stream (stream) into the line string, looking for the deafult gateway.
  while (std::getline(stream, line)) {
    std::istringstream line_steam(line);
    std::string interface_name, destination, gateway;

    // pharse each line interface_name, dest, gateway 
    if (!(line_steam >> interface_name >> destination >> gateway)){
      continue;
    }

    if (interface_name == interface && destination == DEFAULT_GATEWAY_DEST) {
      // convert to hexa from binary
      unsigned int address = std::stoul(gateway, nullptr, 16);

      struct in_addr gateway_addr;
      gateway_addr.s_addr = address;

      std::cout << "Default Gateway: " << inet_ntoa(gateway_addr) << std::endl;
      break;
    }
  }
  close(fd);
}

void NetInfo::printDNSServers(){
  char buffer[BUF];

  int fd = open("/etc/resolv.conf", O_RDONLY);
  if (fd == -1) {
    perror("smash error: open failed");
    return;
  }
  ssize_t b_read = read(fd, buffer, BUF - 1);
  if (b_read <= 0) {
    perror("smash error: read failed");
    close(fd);
    return;
  }

  buffer[b_read] = '\0';

  std::istringstream stream(buffer);
  std::string line;

  bool first = true;
  // reads each line from the string stream (stream) into the line string, looking for the deafult gateway.
  while (std::getline(stream, line)) {
    if (line.find("nameserver") == 0) {
      std::istringstream line_steam(line);
      std::string token, dns;

      if (line_steam >> token >> dns) {
        if (first) {
          std::cout << "DNS Servers: ";
        }
        if (!first) {
          std::cout << ", ";
        }
        std::cout << dns;
        first = false;
      }
    }
  }
  std::cout << "\n";

  close(fd);
}


void NetInfo::printIPSubnet(const std::string &interface){
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
  perror("smash error: socket failed");
    return;
  } 
  struct ifreq inf;     // holds interface_namermation about the network interface

  strncpy(inf.ifr_name, interface.c_str(), IFNAMSIZ - 1);     // copy without buffer overflow the provided ibterface name
  
  // queries the IP address of the specified interface
  if (ioctl(sock, SIOCGIFADDR, &inf) == 0) {
    struct sockaddr_in *ip = (struct sockaddr_in *)&inf.ifr_addr;
    // convert the IP address from binary to a string.
    std::cout << "IP Address: " << inet_ntoa(ip->sin_addr) << std::endl;
  }
  else{
    perror("smash error: ioctl failed");
  }

  // queries the subnet mask address of the specified interface
  if (ioctl(sock, SIOCGIFNETMASK, &inf) == 0) {
    struct sockaddr_in *mask = (struct sockaddr_in *)&inf.ifr_netmask;
    std::cout << "Subnet Mask: " << inet_ntoa(mask->sin_addr) << std::endl;
  }
  else{
    perror("smash error: ioctl failed");
  }
  close(sock);
}

// return true if the provided interfacde name is valid.
bool NetInfo::isInterfaceValid(const std::string &interface) {
  // create new socket, used for communication with the network stack to retrieve interface_namermation about network interfaces.
  int sock = socket(AF_INET, SOCK_DGRAM, 0);    // AF_INET is the address family for IPv4, SOCK_DGRAM is the socket type as datagram-based
  if (sock < 0) {
    perror("smash error: socket failed");
        return false;
  }    
  struct ifreq inf;     // holds interface_namermation about the network interface

  strncpy(inf.ifr_name, interface.c_str(), IFNAMSIZ - 1);     // copy without buffer overflow the provided ibterface name

  // check if the interface is valid using
  // ioctl system call retrieves the IP address of the specified network interface. 0 in case of success.
  if (ioctl(sock, SIOCGIFADDR, &inf) < 0){
    // ENODEV is an error code indicating "No such device," meaning the specified network interface does not exist.
    if (errno == ENODEV){ 
      std::cerr << "smash error: netinfo: interface " << interface << " does not exist" << std::endl;
    } 
    else{
      perror("smash error: ioctl failed");
    }
    close(sock);
    return false;
  }
  close(sock);
  return true;
}

/**---------------------------ExternalCommand---------------------------------*/
// C'tor:
ExternalCommand::ExternalCommand(const char *cmd_line) : Command(cmd_line) {
  this->cmd_line = cmd_line;
  this->cmd_line = _trim(this->cmd_line);
}

void ExternalCommand::execute() {
  bool backgroundFlag = _isBackgroundComamnd(this->cmd_line.c_str());  // check if background
  char* ptr = (char *)this->cmd_line.c_str();
  if (backgroundFlag) {
    _removeBackgroundSign(ptr);
  }

  char* args[COMMAND_MAX_ARGS + 1];
  int numOfArgs = _parseCommandLine(ptr, args);
  if (!numOfArgs) return;
  
  args[COMMAND_MAX_ARGS] = nullptr;

  pid_t pid = fork();
  if (pid == -1) {
    perror("smash error: fork failed");
    return;
  }

  if (pid == 0) {  // Child process
    setpgrp();

      string target = isSimpleInDirectoryExe(args[0]);
      if (containsWildcards(args)) {  // Handle wildcards with bash
      const char* bashPath = "/bin/bash";
      const char* const bashArgs[] = {bashPath, "-c", this->cmd_line.c_str(), nullptr};

      execv(bashPath, const_cast<char**>(bashArgs));
      perror("smash error: execv failed");
      exit(EXIT_FAILURE);
    }
    else if (isInPATHEnvVar(args[0])) {  // Handle commands in PATH
      execvp(args[0], args);
      perror("smash error: execvp failed");
      exit(EXIT_FAILURE);
    }
    else if(!target.empty()) {           //found a command that looks like an executable - check in directory or sub directories 
      string path = findExecutableInCurrentDir(".", target);
      if (!path.empty()) {
        execv(path.c_str(), args);
        perror("smash error: execv failed");
        exit(EXIT_FAILURE);
      }
    }
    //none of the above
    std::cerr << "smash error: command not found: " << args[0] << std::endl;
    exit(EXIT_FAILURE);
  }
  else if (!backgroundFlag){  // Parent process
    int status;
    SmallShell::getInstance().setFGPID(pid);
    if (waitpid(pid, &status, 0) == -1) {
      perror("smash error: waitpid failed");
    }
    SmallShell::getInstance().setFGPID(INITIAL_FG);
  }
  else { // Parent and background
    // bye felicia
    SmallShell::getInstance().getJobs()->addJob(this, pid);
  }

  for (int i = 0; i < numOfArgs; ++i) {
    free(args[i]);
  }
}

/**---------------------------ListDirCommand---------------------------------*/
// C'tor:
ListDirCommand::ListDirCommand(const char *cmd_line) : Command(cmd_line) {
    char *args[COMMAND_MAX_ARGS + 1];
    int numOfArgs = _parseCommandLine(cmd_line, args);

    // too many arguments
    if (numOfArgs > 2) { 
        std::cerr << "smash error: listdir: too many arguments" << std::endl;
        this->target_dir = INVALID_DIR;
    } else if (numOfArgs == 2) {
        this->target_dir = args[1];
    } else {
      // list the currect dir
        this->target_dir = ".";
    }
    _argsFree(numOfArgs, args);
}

void ListDirCommand::execute() {
    if (strcmp(this->target_dir.c_str(), INVALID_DIR) == 0){
      return;
    } 
    int dir_fd = open(this->target_dir.c_str(), O_RDONLY | O_DIRECTORY); // readonly, make sure it is a directory
    // in case of faulure to open. ensures that the starting directory exists and is valid.
    if (dir_fd == -1) {
        perror("smash error: open failed");
        return;
    }
    // recursive function for printing
    listDirectory(dir_fd, target_dir, 0);
    close(dir_fd);
    return;
}

/**
* @param dir_fd       File descriptor for the directory to be listed.
* @param path         Full path of the directory being processed.
* @param indent_level Current indentation level to print the output.
* Reads the contents of a directory using the `SYS_getdents` system call, it identifies
* files and subdirectories, sorts them alphabetically, and prints them with indentation.
* If no directory path is provided, it lists the contents of the current working directory.
* `perror` if:
*  - `stat` fails for any entry. (not dir)
*  - `openat` fails when trying to open a subdirectory.
*  - `SYS_getdents` fails.
*  - If the specified directory does not exist or cannot be accessed,
* `smash error` if:
*  - If more than one argument is provided "smash error: listdir: too many arguments"
*/
void ListDirCommand::listDirectory(int dir_fd, const std::string& path, int indent_level) {
    char buffer[BUF];
    struct stat entry_stat;     // metadata about a file or directory.
    std::vector<std::string> dirs;
    std::vector<std::string> files;

    int num_read = 0; 
    while ((num_read = syscall(SYS_getdents, dir_fd, buffer, BUF)) > 0) {
        for (int current = 0; current < num_read;) {
            // buffer + current points to the start of the current directory entry
            struct linux_dirent *dirent = (struct linux_dirent *)(buffer + current); 
            std::string name = dirent->d_name;     // d_name: name of the file/dir.

            // skip special entries
            if (name == "." || name == "..") {
                current += dirent->d_reclen;    // d_reclen: length of the current entry
                continue;
            }

            std::string full_path = path + "/" + name;

            // use stat to check if the entry is a file or directory
            if (stat(full_path.c_str(), &entry_stat) == -1) {
                perror("smash error: stat failed");
                current += dirent->d_reclen;     // d_reclen: length of the current entry
                continue;
            }

            // sort files and dirs
            if (S_ISREG(entry_stat.st_mode)) {
                files.push_back(name);
            } 
            else {
                dirs.push_back(name);
            }

            current += dirent->d_reclen; // next entry
        }
    }

    if (num_read == -1) {
        perror("smash error: SYS_getdents failed");
        return;
    }

    // sort alphabetically
    std::sort(dirs.begin(), dirs.end());
    std::sort(files.begin(), files.end());

    // print dirs first
    for (const auto& dir : dirs) {
        for (int i = 0; i < indent_level; ++i){
          std::cout << "\t";
        } 
        std::cout << dir << std::endl;

        std::string full_path_dir = path + "/" + dir;
        int sub_dir_fd = open(full_path_dir.c_str(), O_RDONLY | O_DIRECTORY);
        if (sub_dir_fd == -1) {
            perror("smash error: open failed");
            continue;
        }
        // subdirectory recursive call
        listDirectory(sub_dir_fd, full_path_dir , indent_level + 1);
        close(sub_dir_fd);
    }

    // print files
    for (const auto& file : files) {
        for (int i = 0; i < indent_level; ++i) std::cout << "\t";
        std::cout << file << std::endl;
    }
    return;
}

/**---------------------------WhoAmICommand---------------------------------*/
// C'tor:
WhoAmICommand::WhoAmICommand(const char *cmd_line) : Command(cmd_line){
  this->cmd_line = cmd_line;
}
void WhoAmICommand::execute() {
  char *userEnv = getenv("USER");                       //contains username
  if (!userEnv) {
    perror("smash error: getenv failed");
    return;
  }
  char *homeEnv = getenv("HOME");                       //contains home directory of usename
  if (!homeEnv) {
    perror("smash error: getenv failed");
    return;
  }
  std::cout << userEnv << " " << homeEnv << std::endl;
}

/**---------------------------RedirectIOCommand---------------------------------*/
// C'tor:
RedirectIOCommand::RedirectIOCommand(const char *cmd_line) : Command(cmd_line){
  this->cmd_line = cmd_line;
}

void RedirectIOCommand::execute() {
  int i, fileFD;
  char* args[COMMAND_MAX_ARGS + 1];
  int numOfArgs = _parseCommandLine(this->cmd_line.c_str(), args);    // parse the command line
  if (!numOfArgs) return;

  for (i = 0; i < numOfArgs; i++) {                                   // find the index that contains the redirection symbol
    if (!strcmp(args[i], ">") || !strcmp(args[i], ">>")) {
      break;
    }
  }

  string arrow = args[i], firstArg, thirdArg;
  firstArg = _trim(this->cmd_line.substr(0, this->cmd_line.find_first_of(arrow)));
  thirdArg = _trim(this->cmd_line.substr(this->cmd_line.find_first_of(arrow) + arrow.length()));

  if (!arrow.compare(">>")) {                                         // open with seek pointer in the end  
    fileFD = open(thirdArg.c_str(), O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
  }
  else {                                                              // open with seek pointer at the start 
    fileFD = open(thirdArg.c_str(), O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
  }

  _argsFree(numOfArgs, args);                                   // release that bitch

  if (fileFD == -1) { // if no file me sad
    perror("smash error: open failed");
    return;
  }

  int oldSTDOUT = dup(STDOUT_FILENO);                             // duplicate the stdout file descriptor
  if (oldSTDOUT == -1) { //if no stdout then how print before
    perror("smash error: dup failed");
    close(fileFD);
    exit(EXIT_FAILURE);
  }

  if (dup2(fileFD, STDOUT_FILENO) == -1) {                        //change output redirection to the file
    perror("smash error: dup2 failed");
    close(fileFD);
    exit(EXIT_FAILURE);
  }

  SmallShell::getInstance().executeCommand(firstArg.c_str());    //execute the command in the first argument 

  if (dup2(oldSTDOUT, STDOUT_FILENO) == -1) {                    //change output redirection to stdout
    perror("smash error: dup2 failed");
    close(fileFD);
    exit(EXIT_FAILURE);
  }

  if (close(fileFD) == -1) {                                      //close file descriptors
    perror("smash error: close failed");
  }
    if (close(oldSTDOUT) == -1) {
    perror("smash error: close failed");
  }

}

/**---------------------------PipeCommand---------------------------------*/
// C'tor:
PipeCommand::PipeCommand(const char *cmd_line) : Command(cmd_line){
  this->cmd_line = cmd_line;
}

void PipeCommand::execute() {
  int i;
  char* args[COMMAND_MAX_ARGS + 1];
  int numOfArgs = _parseCommandLine(this->cmd_line.c_str(), args);    // parse the command line
  if (!numOfArgs) return;

  for (i = 0; i < numOfArgs; i++) {                                   // find the index that contains the redirection symbol
    if (!strcmp(args[i], "|") || !strcmp(args[i], "|&")) {
      break;
    }
  }

  string pipeType = args[i], firstCommand, secondCommand;
  firstCommand = _trim(this->cmd_line.substr(0, this->cmd_line.find_first_of(pipeType)));
  secondCommand = _trim(this->cmd_line.substr(this->cmd_line.find_first_of(pipeType) + pipeType.length()));

  _argsFree(numOfArgs, args);                                   // release that bitch

  int pipeFd[2];
  if (pipe(pipeFd) == -1) {
    perror("smash error: pipe failed");
    return;
  }

  pid_t pid1 = fork();
  if (pid1 == -1) {
    perror("smash error: fork failed");
    return;
  }
  
  if (pid1 == 0) {
    setpgrp();
    // executes command1
    close(pipeFd[0]);                 // Close read end
    if (!pipeType.compare("|&")) {
      dup2(pipeFd[1], STDERR_FILENO); // Redirect stderr
    } else {
      dup2(pipeFd[1], STDOUT_FILENO); // Redirect stdout
    }
    close(pipeFd[1]);
    SmallShell::getInstance().executeCommand(firstCommand.c_str());
    exit(EXIT_SUCCESS);
  }

  pid_t pid2 = fork();
    if (pid2 == -1) {
        perror("fork");
        return;
    }

  if (pid2 == 0) {
    setpgrp();
    // executes command2
    close(pipeFd[1]); // Close write end
    dup2(pipeFd[0], STDIN_FILENO); // Redirect stdin
    close(pipeFd[0]);
    SmallShell::getInstance().executeCommand(secondCommand.c_str());
    exit(EXIT_SUCCESS);
  }
  // father
  close(pipeFd[0]);
  close(pipeFd[1]);

  waitpid(pid1, nullptr, 0);
  waitpid(pid2, nullptr, 0);
}

/**--------------------------------------------------------------------*/
/**---------------------------SmallShell-------------------------------*/
/**--------------------------------------------------------------------*/
// C'tor:
SmallShell::SmallShell() {
  this->promptName = "smash> ";
  this->currPwd = getcwd(nullptr, 0);
  this->lastPwd = "-1";
  this->built_in_commands = {"alias", "unalias", "chprompt", "showpid", "pwd", "cd", "jobs", "fg", "quit", "kill", "listdir", "whoami", "netinfo"};
  this->jobs = new JobsList();
  this->fg_pid = INITIAL_FG;
}

SmallShell::~SmallShell() {
  delete jobs;
}

void SmallShell::changePrompt(const string newPrompt) {
  this->promptName = newPrompt == "" ? "smash> " : newPrompt + "> ";
}

string SmallShell::getPrompt() {
  return this->promptName;
}

string SmallShell:: getCurrentDirectory() {
  return this->currPwd;
}

JobsList* SmallShell::getJobs() {
  return this->jobs;
}

JobsList::JobEntry *SmallShell::head()
{
  if (!this->jobs->jobs.empty()) {
    return &this->jobs->jobs.front();
  }
  return nullptr;
}

JobsList::JobEntry *SmallShell::tail()
{
  if (!this->jobs->jobs.empty()) {
    return &this->jobs->jobs.back();
  }
  return nullptr;
}

/**
* Creates and returns a pointer to Command class which matches the given command line (cmd_line)
*/
Command *SmallShell::CreateCommand(const char *cmd_line) {
  SmallShell &smash = SmallShell::getInstance();
  char* args[COMMAND_MAX_ARGS];
  string line = replaceAliased(cmd_line, smash.alias_map);     //for some reason doesnt want to parse a const smh
  int numOfArgs = _parseCommandLine(line.c_str(), args);
  if (!numOfArgs) return nullptr;
  
  bool redirectionCommand = isRedirection(numOfArgs, args);
  bool pipeCommand = isPipe(numOfArgs, args);

  const char* cmd_s = line.c_str();

  string firstWord = args[0];                                           //it was already a string and its easy to use
  _argsFree(numOfArgs, args);                                           //free the memory of its sins

  if (pipeCommand) {
    return new PipeCommand(cmd_s);
  }
  else if (redirectionCommand) {                                       
    return new RedirectIOCommand(cmd_s);
  }
  else if (firstWord.compare("alias") == 0) {
    return new aliasCommand(cmd_s, smash.alias_map, smash.built_in_commands, smash.alias_list);
  }
  else if (firstWord.compare("unalias") == 0) {
    return new unaliasCommand(cmd_s, smash.alias_map, smash.alias_list);
  }
  else if (firstWord.compare("chprompt") == 0) { 
    return new CHPromptCommand(cmd_s);
  }
  else if (firstWord.compare("showpid") == 0) {
    return new ShowPidCommand(cmd_s);
  }
  else if (firstWord.compare("pwd") == 0) {
    return new GetCurrDirCommand(cmd_s);
  }
  else if (firstWord.compare("cd") == 0) {
    return new ChangeDirCommand(cmd_s, nullptr);
  }
  else if (firstWord.compare("jobs") == 0) {
    return new JobsCommand(cmd_s, jobs);
  }
  else if (firstWord.compare("fg") == 0) {
    return new FGCommand(cmd_s);
  }
  else if (firstWord.compare("quit") == 0) {
    return new QuitCommand(cmd_s, this->jobs);
  }
  else if (firstWord.compare("kill") == 0) {
    return new KillCommand(cmd_s, this->jobs);
  }
  else if (firstWord.compare("whoami") == 0) {
    return new WhoAmICommand(cmd_s);
  }
   else if (firstWord.compare("listdir") == 0) {
    return new ListDirCommand(cmd_s);
  }
  else if (firstWord.compare("netinfo") == 0) {
    return new NetInfo(cmd_s);
  }
  else {
    return new ExternalCommand(cmd_s);
  }
    return nullptr;
}

// void SmallShell::executeCommand(const char *cmd_line) {
//   Command* cmd = CreateCommand(cmd_line);
//   if(cmd) cmd->execute();
// }


void SmallShell::executeCommand(const char *cmd_line) {
  
  this->jobs->removeFinishedJobs();

  Command* cmd;

  try {
    cmd = CreateCommand(cmd_line);
  } 
  catch (const std::exception &e) {
        perror("smash error: memory allocation failed");
        return;
  }

  if(cmd) cmd->execute();
}


void SmallShell::setCurrentDirectory(const std::string newDir){
  this->currPwd = newDir;
}

void SmallShell::setFGPID(pid_t pid) {
  this->fg_pid = pid;
}
    
pid_t SmallShell::getFGPID() {
  return this->fg_pid;
}

/**--------------------------------------------------------------------*/
/**-----------------------------Jobs-----------------------------------*/
/**--------------------------------------------------------------------*/

bool JobsList::isEmpty() {
  return this->jobs.empty();
}

JobsList::JobsList() {
  this->nextJobID = 1;
}

/**---------------------------JobEntry---------------------------------*/
// C'tor:
JobsList::JobEntry::JobEntry(int ID,pid_t pid, Command* command, bool isActive) {
  this->ID = ID;
  this->pid = pid;
  this->command = command;
  this->isActive = isActive;
}

void JobsList::addJob(Command *cmd, pid_t pid, bool isActive) {
  removeFinishedJobs();
  getLastJob(&this->nextJobID);
  this->jobs.emplace_back(++nextJobID, pid, cmd, isActive);
}

void JobsList::printJobsList() {
  removeFinishedJobs();
  for (const auto& job : jobs) {
    job.printEntry();
  }
}

void JobsList::JobEntry::printEntry() const {
  std::cout << "[" << this->ID << "] " << this->command->getLine() << std::endl;
}

void JobsList::killAllJobs() {
  for (auto& job : jobs) {
    job.isActive = false;
    if(kill(job.getPID(), SIGKILL) == -1){
      perror("smash error: kill failed");
    }
    else{
      std::cout << "smash: process " << job.getPID() <<" was killed" << std::endl;
    }
  }
  removeFinishedJobs();
}

void JobsList::removeFinishedJobs() {
  for (auto it = jobs.begin(); it != jobs.end();) {
    int status = 0;
    pid_t result = waitpid(it->getPID(), &status, WNOHANG);
    if ((WIFSIGNALED(status)) || (result == it->getPID() && (WIFEXITED(status)))) {
      it = jobs.erase(it);
    }
    else {
      ++it;
    }
  }
}

JobsList::JobEntry *JobsList::getJobById(int jobId){
  for (auto& job : jobs) {
    if (job.equalsToID(jobId)) {
      return &job;  // Return the address of the job if found
    }
  }
  return nullptr;  // Return nullptr if no job with the given ID is found
}

bool JobsList::JobEntry::equalsToID(int jobId) {
  return this->ID == jobId;
}

void JobsList::removeJobById(int jobId) {
  for (auto it = jobs.begin(); it != jobs.end(); ++it) {
    if (it->equalsToID(jobId)) {
      jobs.erase(it);
      break;
    }
  }
}

// changes the lastjobsid to the last taken. if empty sets 0. 
// returns the last job in the list
JobsList::JobEntry *JobsList::getLastJob(int *lastJobId){
  if (jobs.empty()) {
    *lastJobId = 0;
    return nullptr;
  }
  *lastJobId = jobs.back().getID();
  return &jobs.back();
}

int JobsList::getLastJobID(){
  return this->jobs.back().getID();
}

int JobsList::JobEntry::getID(){
  return this->ID;
}

pid_t JobsList::JobEntry::getPID()
{
    return this->pid;
}

string JobsList::JobEntry::getCommandLine()
{
    return this->command->getLine();
}

