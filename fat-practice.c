#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>




/////////////////////////////////////////////////////// SIZES //////////////////////////////////////////////////////
#define DISK_SIZE 10485759
#define BLOCK_SIZE 4096
#define CLUSTER_TOTALNUM 2560
#define ENDOFFILE 3000
#define FREE 4000
#define PATH_MAX 4096 //unix definition.
#define COM_LENGTH 4100

// errors
#define ENOTPERM 1 //operation not permitted
#define ENOENT 2 //no such file or directory
#define EACCES 13 //permission denied
#define EEXIST 17 //file already exists
#define ENOTDIR 20 //not a directory
#define EISDIR 21 //is a directory
#define ENOSPC 28 //no more space
#define EMLINK 31 //too many links
#define ENAMETOOLONG 36 //file name too long
#define ENOTEMPTY 39 //directory not empty
#define ELOOP 40 //too many links


/////////////////////////////////////////////////////// DEFINITION OF DATA STRUCTURES  /////////////////////////////
struct fat_superblock
{
  // if uninitialized                                                                                          
  int fat_location;
  int fat_size;
  int data_location; //root dir first cluster location                                                         
  int data_size;
  //  int freelist_location; //essentially pointer to the first free cluster                                   
};

union {
  struct fat_superblock s;
  char pad[BLOCK_SIZE];
} superblock;


typedef struct
{
  int cur;
  struct free_node *next;
}free_node;

typedef struct
{
  int first_cluster;
  int file_size; // in bytes                                                                                   
  int dir_type; // 0 for empty, 1 for directory, 2 for file                                                    
  int read_only; // 0 for read and write, 1 for read only                                                      
  char name[32];
}dir_entry;




/////////////////////////////////////////////////////// GLOBAL VARIABLES ////////////////////////////////////////////////////////
char disk_path[1024];
FILE *disk;
int fd_disk;


int fat[3072];


int last_free = 0;

char rootdir[PATH_MAX];
char workdir[PATH_MAX];
int workdir_clusternum;





////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////// CODE ///////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////









// When theres no disk, create generic disk                                                                                            
void create_init_disk()
{
  // write out superblock                                                                                                              
  fd_disk = fileno(disk);
  pwrite(fd_disk,&superblock,BLOCK_SIZE,0);

  // write out fat                                                                                                                     
  memset(&fat,0,sizeof(fat));
  for(int i = 0; i < 2556; i++){
    // fill buffers with value for free
    fat[i] = FREE;
  }
  //make sure root dir cluster is allocated                                                                                            
  fat[0] = ENDOFFILE;

  pwrite(fd_disk,fat,(sizeof(int)*3072),BLOCK_SIZE);


  // write out root dir                
  dir_entry dir_entries[4096]; //rather arbitrary size, but point is its bigger than block size 
  memset(dir_entries, 0, sizeof(dir_entries));
  //creating . 
  dir_entries[0].first_cluster = 4;
  dir_entries[0].file_size = 2;
  dir_entries[0].dir_type = 1;
  memset(dir_entries[0].name,0,32);
  strcpy(dir_entries[0].name,".");
  // creating .. 
  dir_entries[1].first_cluster = 4;
  dir_entries[1].file_size = 2;
  dir_entries[1].dir_type = 1;
  memset(dir_entries[1].name,0,32);
  strcpy(dir_entries[1].name,"..");
  pwrite(fd_disk, &dir_entries, 4096, (4*4096));

}





static void* fat_init()
{
  // find where "disk" file should be 
  memset(disk_path, '\0', sizeof(disk_path));
  getcwd(disk_path, sizeof(disk_path));
  strcat(disk_path, "/fat_disk");

  // see if "disk" file is initialized  
  if((disk = fopen(disk_path, "r+"))){
    // if it is, read in superblock   
    fd_disk = fileno(disk);
    pread(fd_disk,&superblock,sizeof(superblock),0);

    pread(fd_disk,&fat, sizeof(fat),BLOCK_SIZE);

  }else{
    // if it isn't, create file and write out initial superblock 
    disk = fopen(disk_path, "w+");
    fseek(disk, DISK_SIZE, SEEK_SET);
    fputc('\0', disk);

    // initial locations for fat and data 
    superblock.s.fat_location = 1;
    superblock.s.fat_size = 3;
    superblock.s.data_location = 4;
    superblock.s.data_size = 2556;
    // superblock.s.freelist_location = 5;

    // write out initialized disk  
    create_init_disk();

  }
  
  // set root and working directories
  memset(rootdir,0,PATH_MAX);
  memset(workdir,0,PATH_MAX);
  strcpy(rootdir,"/");
  strncpy(workdir,rootdir,PATH_MAX);
  workdir_clusternum = 4;
  return NULL;
}




int helper(char *path, char *ret){
  memset(ret,0,32);
  char *ptr = strchr(path, '/');
  if(ptr) {
    int index = ptr - path;
    if (index > 32) return -2;

    strncpy(ret,path,index);
    return index+1;
  }else{ return -1;}
  return 0;
}




// return 0 if path exists, -1 if path doesn't exist, -2 if path name is too long, -3 if component of path is file
// later, can remove last and just find it from path in this function
static int dir_exists(char *path, int cluster_num, dir_entry *de, char *last)
{

  // bring in all the dir entries 
  dir_entry dir_entries[4096];//[((4096/sizeof(dir_entries)) + 1)]; 
  memset(dir_entries, 0, sizeof(dir_entries));
  pread(fd_disk, &dir_entries, 4096, (4096*cluster_num));


  // name of the next directory in path to look for
  //char* token = strtok(path, "/");
  char token[32];
  int index;
  // helper returns wrong result when path = token, so
  if((strcmp(path,last) != 0) && (strlen(path) != strlen(last))){
    index = helper(path,token);
    // if name is too long 
    if(index == -2) return -2;
    // should never return -1, but... 
    if(index == -1) return -1;
  } else {

    strcpy(token,path);
  }
  int isFile = 0;
  //search through dir_entries in current directory to see if the token is there
  for (int i = 0; i < 4096; i++){
    if(dir_entries[i].dir_type !=0){
      if(strcmp(dir_entries[i].name, token) == 0){ //if we find a match
        if ((strcmp(token,last) ==0) && (strlen(path) == strlen(last))){ //if last component     
          strcpy(de->name,dir_entries[i].name);
          de->first_cluster = dir_entries[i].first_cluster;
          de->file_size = dir_entries[i].file_size;
          de->dir_type = dir_entries[i].dir_type;
          return 0;
        }else{
          if (dir_entries[i].dir_type == 0) isFile = 1; //if match is file
          else{ //if match is dir
            char path2[strlen(path)];
            for (int j = 0; j < strlen(path); j++){
              path2[j] = path[j+index];
            }
            return dir_exists(path2, dir_entries[i].first_cluster, de, last);
          }
        }
      }
    }
  }
  if(isFile == 1) return -3;
  de = NULL;
  return -1;
}




/*
  key: finish the find_dir method, getattr should check if target dir exists, if so 
  fill the stat struct *stbuf with the information in the dir_entry and return
  if desired dir doesn't exist, return -ENOENT?
  mkdir is gonna suck 
*/
//return file attributes, for path name, should fill in the elements of the stat struc 
//if a field is meaningless then it should be set to 0
static int fat_getattr(const char *path, struct stat *stbuf)
{

  int res = 0;

  memset(stbuf, 0, sizeof(struct stat));

  // see if name is too long
  char *mayberoot = strdup(path);

  if((strlen(basename(mayberoot))>32) || (strlen(path) > PATH_MAX)){
    return -ENAMETOOLONG;
  }

  // if root  
  if (strcmp(path, "/") == 0) {
    // get directory size, and the rest is the usual
    dir_entry dir_entries[4096];//[((4096/sizeof(dir_entries)) + 1)];
    memset(dir_entries, 0, sizeof(dir_entries));
    pread(fd_disk, &dir_entries, 4096, (4096*4));

    stbuf -> st_mode = S_IFDIR | 0755;
    stbuf -> st_nlink = dir_entries[0].file_size;
    stbuf -> st_size = 4096;
    stbuf -> st_blocks = 1;
    return res;
  } else {

    // prepare for dir_exists call 
    char path2[PATH_MAX];
    strcpy(path2,path);
    if(path2[0] == '/'){
      // remove root
      memmove(path2, path2+1, sizeof(path2)-1); //same fix as in getattr 
    }
    // get name of dir we want    
    char *dup = strdup(path2);
    char *last;
    last = basename(dup);

    int cluster_num = 4;
    dir_entry de;

    //if path doesn't exist or name is too long, return error   
    int call_rslt = dir_exists(path2,cluster_num,&de, last);
    if (call_rslt == -1) return -ENOENT;
    if (call_rslt == -2) return -ENAMETOOLONG;
    if (call_rslt == -3) return -ENOTDIR;
    else if (de.dir_type == 1) { //if path does exist, return correct attributes  
      stbuf -> st_mode = S_IFDIR | 0755;
      stbuf -> st_nlink = de.file_size;
      stbuf -> st_size = 4096;
      stbuf -> st_blocks = 1;
    }else{
      // check permissions    
      if(de.read_only == 1) stbuf -> st_mode = S_IFREG | 0444;
      else stbuf->st_mode = S_IFREG | 0666;
      stbuf -> st_nlink = 1;
      stbuf -> st_size = de.file_size; // by multiple of blocks
      int b = (de.file_size/4096);
      if((de.file_size%4096) != 0) b++;
      stbuf -> st_blocks = b;
    }
  }
  return res;
}





static int fat_access(const char *path, int mask)
{

  // see if name is too long
  if((strlen(basename(strdup(path)))>32) || (strlen(path) > PATH_MAX)){
    return -ENAMETOOLONG;
  }
  

  // i am groot
  if (strcmp(path, "/") == 0) {
    return 0;
  } else {

    // prepare for dir_exists call
    char path2[PATH_MAX];
    strcpy(path2,path);
    if(path2[0] == '/'){
      // path2++;
      memmove(path2, path2+1, sizeof(path2)-1); //same fix as in getattr
    }

    // get name of dir we want
    char *dup = strdup(path2);
    char *last;
    last = basename(dup);

    int cluster_num = 4;
    dir_entry de;

    // if path doesn't exist, return error
    int call_rslt = dir_exists(path2,cluster_num,&de, last);
    if (call_rslt == -1) return -ENOENT;
    if (call_rslt == -2) return -ENAMETOOLONG;
    if (call_rslt == -3) return -ENOTDIR;
    else if (de.dir_type == 1){ // if path does exist, check for permissions
      // but all directories are 0755 so...
      return 0;
    }else{
      if((mask & W_OK) && (de.read_only == 1)){
	return -EACCES;
      }
      return 0;
    }
  }

}






static int fat_mkdir(const char* path, mode_t mode)
{

  // separate path and name of last dir
  char *dup1 = strdup(path);
  char *dup2 = strdup(path);
  char *parent_path = dirname(dup1);
  char *dir_name = basename(dup2);


  // can't make root
  if(strcmp(path,"/") == 0) return -ENOENT;
  
  // see if name is too long
  if((strlen(basename(strdup(path)))>32) || (strlen(path) > PATH_MAX)){
    return -ENAMETOOLONG;
  }
  
  dir_entry parent_de;
  dir_entry grandparent_de;



  char path2[PATH_MAX];
  strcpy(path2,path);
  if(path2[0] == '/'){
    // path2++;
    memmove(path2, path2+1, sizeof(path2)-1); //same fix as in getattr
  }
  // check if directory already exists (using parent_de, but not looking for parent directory entry, just for convenience)
  int call_rslt = dir_exists(path2, 4, &parent_de, dir_name);
  if (call_rslt == -2) return -ENAMETOOLONG;
  if (call_rslt == 0) return -EEXIST;
  if (call_rslt == -3) return -ENOTDIR;
  //  memset(&parent_de,0,sizeof(parent_de));


  dir_entry dir_entries[4096];//[((4096/sizeof(dir_entries)) + 1)];
  memset(dir_entries, 0, sizeof(dir_entries));

  // check if path leading to directory doesn't exist
  if(strcmp(parent_path,"/") == 0) { // i am groot
    //get appropriate directory entry for root
    pread(fd_disk, &dir_entries, 4096, (4096*4));
    parent_de.first_cluster = 4;
    parent_de.file_size = dir_entries[0].file_size;
    strcpy(parent_de.name,".");
    parent_de.dir_type = 1;
  } else { //if not root,
    // prepare for dir_exists call

    strcpy(path2,parent_path);
    if(path2[0] == '/'){
      // path2++;
      memmove(path2, path2+1, sizeof(path2)-1); //same fix as in getattr
    }

    // get name of dir we want
    char *dup = strdup(path2);
    char *last;
    last = basename(dup);

    int call_rslt = dir_exists(path2, 4, &parent_de, last);
    if (call_rslt == -1) return -ENOENT; //doesn't exist

    // if parent isn't groot, we know it has a grandparent
    // all we need is the cluster number, so get it from .. of parent
    pread(fd_disk,&dir_entries, 4096, (4096*parent_de.first_cluster));
    grandparent_de.first_cluster = dir_entries[1].first_cluster;
  }
  // check if there is space for more directory entries
  int max = (4096/ sizeof(dir_entry));
  if (parent_de.file_size >= max) return -ENOSPC;






  for(int i= 0; i <2556; i++){
    // if we find an empty cluster
    if(fat[i] == 4000){
      // set cluster to endoffile and write out
      fat[i] = 3000;
      pwrite(fd_disk,&fat, (4096*3), 4096);


      // FOR NEW DIR ENTRY IN NEW CLUSTER
      // creating new dir entry
      memset(dir_entries,0,sizeof(dir_entries));

      dir_entry de;
      de.first_cluster = i+4;
      de.file_size = 2;
      de.dir_type = 1;
      memset(de.name,0,sizeof(de.name));
      strcpy(de.name,".");
      dir_entries[0] = de;

      dir_entries[1].first_cluster = parent_de.first_cluster;
      dir_entries[1].file_size = ((parent_de.file_size)+1);
      dir_entries[1].dir_type = 1;
      strcpy(dir_entries[1].name,"..");

      //write out new directory cluster
      pwrite(fd_disk,&dir_entries, 4096, (4096*(i+4)));



      // FOR CHANGING PARENT CLUSTER
      // read in parent cluster
      pread(fd_disk,&dir_entries, 4096, (4096*parent_de.first_cluster));

      // change parent cluster
      strcpy(de.name,dir_name);
      for (int j = 0; j < max; j++){
	if(dir_entries[j].dir_type == 0){
	  dir_entries[j] = de;
	  break;
	}
      }
      // update size in .
      dir_entries[0].file_size++;

      // check if parent cluster is root
      if(parent_de.first_cluster == 4){
	// then also have to update ..
	dir_entries[1].file_size++;
      }
      // write out parent cluster
      pwrite(fd_disk,&dir_entries, 4096, (4096*parent_de.first_cluster));



      // POSSIBLY CHANGING GRANDPARENT CLUSTER
      // if there is a grandparent
      if(strcmp(parent_path,"/") != 0){
	// read in grandparent cluster
	pread(fd_disk,&dir_entries, 4096, (4096*grandparent_de.first_cluster));
	// change parent dir entry in gp cluster
	for (int j = 0; j < 4096; j++){
	  //if valid dir entry
	  if(dir_entries[j].dir_type != 0){
	    // check if is the parent dir entry
	    if(strcmp(dir_entries[j].name,parent_de.name) == 0){
	      dir_entries[j].file_size++;
	    }
	  }
	}
	pwrite(fd_disk,&dir_entries,4096,(4096*grandparent_de.first_cluster));
      }


      return 0;
    }
  }

  return -ENOSPC;
}






static int fat_mknod(const char* path, mode_t mode, dev_t rdev)
{
  //check mode 
  if(S_ISREG(mode)){

    // separate path and name of last dir
    char *dup1 = strdup(path);
    char *dup2 = strdup(path);
    char *parent_path = dirname(dup1);
    char *file_name = basename(dup2);

    // can't make root 
    if(strcmp(path,"/") == 0) return -ENOENT;
    // see if name is too long 
    if((strlen(basename(strdup(path)))>32) || (strlen(path) > PATH_MAX)){
      return -ENAMETOOLONG;
    }

    dir_entry parent_de;
    dir_entry grandparent_de;



    char path2[PATH_MAX];
    strcpy(path2,path);
    if(path2[0] == '/'){
      // path2++;
      memmove(path2, path2+1, sizeof(path2)-1); //same fix as in getattr
    }
    // check if directory already exists (using parent_de, but not looking for parent directory entry, just for convenience)
    int call_rslt = dir_exists(path2, 4, &parent_de, file_name);
    if (call_rslt == -2) return -ENAMETOOLONG;
    if (call_rslt == 0) return -EEXIST;
    if (call_rslt == -3) return -ENOTDIR;
    //  memset(&parent_de,0,sizeof(parent_de));

    dir_entry dir_entries[4096];//[((4096/sizeof(dir_entries)) + 1)];
    memset(dir_entries, 0, sizeof(dir_entries));

    // check if path leading to directory doesn't exist
    if(strcmp(parent_path,"/") == 0) { // i am groot
      //get appropriate directory entry for root
      pread(fd_disk, &dir_entries, 4096, (4096*4));
      parent_de.first_cluster = 4;
      parent_de.file_size = dir_entries[0].file_size;
      strcpy(parent_de.name,".");
      parent_de.dir_type = 1;
    } else { //if not root,
      // prepare for dir_exists call 

      strcpy(path2,parent_path);
      if(path2[0] == '/'){
        // path2++;
        memmove(path2, path2+1, sizeof(path2)-1); //same fix as in getattr
      }

      // get name of dir we want
      char *dup = strdup(path2);
      char *last;
      last = basename(dup);

      int call_rslt = dir_exists(path2, 4, &parent_de, last);
      if (call_rslt == -1) return -ENOENT; //doesn't exist

      // if parent isn't groot, we know it has a grandparent
      // all we need is the cluster number, so get it from .. of parent
      pread(fd_disk,&dir_entries, 4096, (4096*parent_de.first_cluster));
      grandparent_de.first_cluster = dir_entries[1].first_cluster;
    }
    // check if there is space for more directory entries 
    int max = (4096/ sizeof(dir_entry));
    if (parent_de.file_size >= max) return -ENOSPC;


    for(int i= 0; i <2556; i++){
      // if we find an empty cluster 
      if(fat[i] == 4000){
        // set cluster to endoffile and write out
        fat[i] = 3000;
        pwrite(fd_disk,&fat, (4096*3), 4096);


        // FOR NEW DIR ENTRY IN NEW CLUSTER
        // creating new dir entry 
        dir_entry de;
        de.first_cluster = i+4;
        de.file_size = 0;
        de.dir_type = 2;


        //check mode to get permissions  
        if (mode == (S_IFREG | 0444)) de.read_only = 1;
        else de.read_only = 0;
        memset(de.name,0,sizeof(de.name));
        strcpy(de.name,file_name);


        // FOR CHANGING PARENT CLUSTER 
        // read in parent cluster
        pread(fd_disk,&dir_entries, 4096, (4096*parent_de.first_cluster));

        // change parent cluster
        for (int j = 0; j < max; j++){
          if(dir_entries[j].dir_type == 0){
            dir_entries[j] = de;
            break;
          }
        }
        // update size in .
        dir_entries[0].file_size++;

        // check if parent cluster is root
        if(parent_de.first_cluster == 4){
          // then also have to update ..
          dir_entries[1].file_size++;
        }
        // write out parent cluster
        pwrite(fd_disk,&dir_entries, 4096, (4096*parent_de.first_cluster));

        // POSSIBLY CHANGING GRANDPARENT CLUSTER
        // if there is a grandparent 
        if(strcmp(parent_path,"/") != 0){
          // read in grandparent cluster
          pread(fd_disk,&dir_entries, 4096, (4096*grandparent_de.first_cluster));
          // change parent dir entry in gp cluster     
          for (int j = 0; j < 4096; j++){
            //if valid dir entry
            if(dir_entries[j].dir_type != 0){
              // check if is the parent dir entry
              if(strcmp(dir_entries[j].name,parent_de.name) == 0){
                dir_entries[j].file_size++;
              }
            }
          }
          pwrite(fd_disk,&dir_entries,4096,(4096*grandparent_de.first_cluster));
        }
        return 0;
      }
    }
    return -ENOSPC;
  }
  return -EACCES;
}






///////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////// user command interpretation ///////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////

int description_helper(char *path, char *ret){
  int i = 0;
  for(int k = 0; strchr((path+i),'-') != NULL; k++){
    
    int index = strchr((path+i),'-')-(path);
    ret[k] = path[index+1];
    printf("%d, %c", index, ret[k]);
    i = index + 1;
  }
  //if i is 0, no descriptions
  if(i == 0) return 0;
  else{
    //else adjust path and return 
    path = &path[i+2]; //after the description character, and ' '
    return i;
  }
}

static int instruct_ls(char *path){

  char descriptions[PATH_MAX];
  int num_descript = 0;

  // no descriptions
  if((path == NULL) || ((num_descript = description_helper(path,descriptions)) == 0)){
    // read in subfiles of working directory
    

    dir_entry de;
    int err = 0;

    // in working directory
    if(path == NULL){
      // see if working directory is root
      if(strcmp(workdir,rootdir) == 0){
	err = dir_exists(".",4,&de,".");

      }
      else {
      char *dup = strdup(workdir);
      char *dup1 = strdup(rootdir);
      char *last;
      last = basename(dup);
      
      err = dir_exists(dup1,4,&de,last);
      }
    }
    else {    // in another directory

      char *dup = strdup(path);
      char *last;
      last = basename(dup);

      // concat the workdir with further path
      // first check if the path + workdir exceeds path name length
      if((strlen(path) + strlen(workdir)) > PATH_MAX) return -ENAMETOOLONG;

      char *dup1 = strdup(workdir);
      strcat(dup1,path);

      err = dir_exists(dup1,4,&de,last);

    }

    if(err != 0) return err;

    // print out nonhidden subfiles of de.
    dir_entry dir_entries[4096];
    memset(dir_entries, 0, sizeof(dir_entries));
    pread(fd_disk, &dir_entries, 4096, (4096*de.first_cluster));

    for(int i = 0; i <4096; i++){
      // if a dir entry isn't hidden (ie starts with .)
      if((dir_entries[i].dir_type != 0) && (!(dir_entries[i].name[0] == '.'))){
	printf("%s\t\t", dir_entries[i].name);
      }
    }
    printf("\n");
  }
  

  // if there are descriptions, switch by case (if statements in c)
  return 0;
}



static int instruct_cd(char *path){

  char descriptions[PATH_MAX];
  int num_descript = 0;


  // no descriptions
  if((num_descript = description_helper(path,descriptions)) == 0){
    // read in subfiles of working directory
    
    dir_entry de;
    
    // in working directory
    if(path == NULL){
      //
      //change working directory to root
      strcpy(workdir,rootdir);
      workdir_clusternum = 4;
    }
    else {    // in another directory

      char *dup = strdup(path);

      int err = 0;

      // check if path is from root
      if(path[0] == '/') err = fat_access(path,R_OK);
      // or if working directory is root
      else if (strcmp(workdir,rootdir) == 0){
	dup = strdup(rootdir);
	strcat(dup,path);
	err = fat_access(dup,R_OK);
      }      
      else { // if path is from working directory, concat with workdir path
	// see if path doesn't exceed size
	if((strlen(workdir) + strlen(path)) > PATH_MAX) return -ENAMETOOLONG;
	dup = strdup(workdir);
	strcat(dup,"/");
	strcat(dup,path);
	err = fat_access(dup,R_OK);
	
      }
      
      if(err != 0) return err;
      
      // change working directory to this one
      strcpy(workdir,dup);
      workdir_clusternum = de.first_cluster;
    }

  }
  


  // if there are descriptions, switch by case (if statements in c)
  return 0;
}




static int instruct_mkdir(char *path){
  
char descriptions[PATH_MAX];
  int num_descript = 0;


  // no descriptions
  if((num_descript = description_helper(path,descriptions)) == 0){
    // read in subfiles of working directory
    
    
    // no path?
    if(path == NULL) return -ENOENT;

    else {    // making new directory
      //check permission in parent directory

      int err = 0;
      // check if path is from root
      if(path[0] == '/') err = fat_mkdir(path,(S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH));
      // or if working directory is root
      else if (strcmp(workdir,rootdir) == 0) err = fat_mkdir(strcat(strdup(rootdir),path),(S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH));
      else { // if path is from working directory, concat with workdir path
	// see if path doesn't exceed size
	if((strlen(workdir) + strlen(path)) > PATH_MAX) return -ENAMETOOLONG;
	char *dup = strdup(workdir);
	strcat(dup,"/");
	strcat(dup,path);
	err = fat_mkdir(dup,(S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH));
		
      }
      if (err != 0) return err;
    }

  }
  

  // if there are descriptions, switch by case (if statements in c)
  return 0;


}







////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////  MAIN MAIN MAIN MAIN ///////////////////////////////////////////////////// 
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



int main(){

  // initialize
  fat_init();

  // tell user how to use
  printf("\nFAT Filesystem (c)2020  Jae Surh: \nVirtual simulation of a filesystem. Memory stored on a file called 'fat_disk'.\nSupports Unix commands. Place the path after the command and descriptions, if any.\nType 'quit' then enter, to quit.\n\n");



  //prepare for command
  char command[COM_LENGTH];

  int err = 0;

  // continuously get commands, until quit.
  fgets(command, COM_LENGTH, stdin);
  while (strcmp(command, "quit\n") != 0) {

    // get rid of \n at the end of the string
    command[(strlen(command)-1)] = 0;


    // if command has no path or description
    if(strchr(command, ' ') == NULL){
      
      if(strcmp(command,"ls") == 0){
	instruct_ls(NULL);
      }
      else if(strcmp(command,"cd") == 0){
	instruct_cd(NULL);
      }
      
    }
    else{

      //split into command and rest
      char mand[COM_LENGTH];
      strcpy(mand,strchr(command, ' ')+1);

      int index = strchr(command, ' ') - command;
      char com[COM_LENGTH];
      strncpy(com,command,index);


      if(strcmp(com,"ls") == 0){
	err = instruct_ls(mand);
      }
      else if(strcmp(com,"cd") == 0){
	err = instruct_cd(mand);
      }
      else if(strcmp(com,"more") == 0){
	
      }
      else if(strcmp(com,"less") == 0){
	
      }
      else if(strcmp(com,"mkdir") == 0){
	err = instruct_mkdir(mand);
      }
      else if(strcmp(com,"rmdir") == 0){
	
      }
      else if(strcmp(com,"echo") == 0){
	
      }
      else {
	printf("Error: Command not recognized.\n");
      }
    
    }


    ///////////////////// ERRORS!!! ///////////////////////
    if(err == -ENOTPERM) printf("Operation not permitted.\n");
    if(err == -ENOENT) printf("File doesn't exist.\n");
    if(err == -EACCES) printf("Permission denied.\n");
    if(err == -EEXIST) printf("File already exists.\n");
    if(err == -ENOTDIR) printf("Not a directory.\n");
    if(err == -EISDIR) printf("Is an existing directory.\n");
    if(err == -ENOSPC) printf("No more space.\n");
    if(err == -EMLINK) printf("Too many links.\n");
    if(err == -ENAMETOOLONG) printf("File or path name too long.\n");
    if(err == -ENOTEMPTY) printf("Directory not empty.\n");
    if(err == -ELOOP) printf("Too many links.\n");

    printf("%s user: ", workdir);
    // next command. bring it on!
    memset(command,0,200);
    fgets(command, 200, stdin);

  }
}



