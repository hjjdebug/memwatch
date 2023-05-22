// Usage ./memwatch Appname
// modify by: hjjdebug
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <libgen.h>
#include <sys/sysinfo.h>
#include <time.h>
#include <signal.h>
#include <execinfo.h>

#pragma GCC diagnostic ignored "-Wunused-result"

///////////////////////////trap definition begin ///////////////
#define SIZE 100
void *buffer[SIZE];
//定义该宏, 可以拿到出错时,访问的地址是什么.
#define USE_SIG_ACTION
#ifdef USE_SIG_ACTION
void fault_trap(int n,siginfo_t *siginfo,void *myact) 
#else
void fault_trap(int n )
#endif
{ 
	(void) n;
#ifdef USE_SIG_ACTION
	(void) myact;
	printf("Fault address:%p\n",siginfo->si_addr);
#endif
	int num = backtrace(buffer, SIZE);
	char **calls = backtrace_symbols(buffer, num);
	for (int i = 0; i < num; i++) 
		printf("%s\n", calls[i]);
	exit(1);
} 

#ifdef USE_SIG_ACTION
void setuptrap() { 
	struct sigaction act;
	sigemptyset(&act.sa_mask);
	act.sa_flags=SA_SIGINFO;
	act.sa_sigaction=fault_trap;
	sigaction(SIGSEGV,&act,NULL);
} 
#endif
///////////////////////////trap definitioa end ///////////////

void printTime()
{
	time_t now;
	struct tm *tm_now;
	now = time(NULL);
	tm_now=localtime(&now);
//	printf("localtime:%s\n",asctime(tm_now));
	printf("time:%02d:%02d:%02d\n",tm_now->tm_hour,tm_now->tm_min,tm_now->tm_sec);
}

/*Function open the statm file for the Application and monitor the usage constantly*/
int CheckUsage(const char *filename)
{

	struct sysinfo info;

	if( sysinfo(&info)!= 0 )
		perror("Sysinfo\n");

	unsigned long TotalRam = 0;
	TotalRam = (info.totalram * info.mem_unit) / 1024;

	//  printf("%10s %12s %13s","Total Ram","Application","Percentage\n");
	unsigned long resident_old=0;
	unsigned long max_resident=0;
	unsigned long vsize_old=0;

	while (1) {

		FILE *fp;
		fp = fopen(filename,"r");

		if(fp == NULL){
			fprintf(stderr,"Application closed or File '%s' does not exists\n", filename);
			return 1;
		}

		unsigned long virtual_size=0;
		unsigned long resident=0;
		unsigned long share=0;
		unsigned long text=0;
		unsigned long lib=0;
		unsigned long data=0;
		unsigned long dt=0;

		fscanf(fp,"%lu %lu %lu %lu %lu %lu %lu",&virtual_size, &resident, &share, &text, &lib, &data, &dt);

		unsigned long physicalUsage = (resident * getpagesize());
		unsigned long virtualUsage = (virtual_size * getpagesize());
		if(resident > max_resident) max_resident = resident;
		unsigned long maxPhyUsage = (max_resident * getpagesize());

		float phyPercentage = 0.0f;
		phyPercentage = (float)(((float)physicalUsage/1024)/(float)TotalRam)*100;
		float virPercentage = 0.0f;
		virPercentage = (float)(((float)virtualUsage/1024)/(float)TotalRam)*100;
		float maxPhyPercent = 0.0f;
		maxPhyPercent = (float)(((float)maxPhyUsage/1024)/(float)TotalRam)*100;


		//      printf("%10lukB %10lukB %10.3f%%\n", TotalRam, physicalUsage/1024, phyPercentage);
		if((resident != resident_old) || (virtual_size != vsize_old) )
		{
			resident_old = resident;
			vsize_old = virtual_size;
			printTime();
			printf("%10lu %10.3f%%, %10lu %10.3f%%, %10lu %10.3f%%\n", \
					virtualUsage, virPercentage, \
					physicalUsage, phyPercentage, \
					maxPhyUsage, maxPhyPercent);
		}

		fclose(fp);

		sleep(1);
	}

	return 1;
}

int main(int argc, char *argv[])
{
  if(argc==1){
      fprintf(stderr,"Usage %s <program>\n", argv[0]);
      exit(EXIT_FAILURE);
  }
  setuptrap();
  const char *AppName = argv[1];


  DIR *procDir  = opendir("/proc");
  if (procDir == NULL){
      fprintf(stderr,"Can't Open /proc directory \n");
      return 1;
  }

  struct dirent *pProcDirEnt = NULL;
  char buf[384] = "";
  while ((pProcDirEnt = readdir(procDir)) != NULL)
  {
	  /*skip . and ..*/
	  if(strlen(pProcDirEnt->d_name)<=2)continue;

	  // DT_DIR(4),DT_REG(8),DT_LINK(10)
	  /*skip file and symlink directory*/
	  if(((int)pProcDirEnt->d_type == 8) || ((int)pProcDirEnt->d_type == 10)) continue;

	  //打开下一层子目录
	  sprintf(buf,"/proc/%s", pProcDirEnt->d_name);
	  /*Open Directory inside the proc directory*/
	  DIR *subdir = opendir(buf);
	  struct dirent *subdir_ent = NULL;
	  while ((subdir_ent = readdir(subdir))!=NULL)
	  {
		  if (strlen(subdir_ent->d_name)<=2 )continue;
		  if((subdir_ent->d_type == 4) || (subdir_ent->d_type == 10))continue;
		  //          printf("filename: %s \n", subdir_ent->d_name);

		  if(strcmp(subdir_ent->d_name,"cmdline") == 0)
		  {
			  char cmdline[1024] = "";
			  sprintf(cmdline,"/proc/%s/%s",pProcDirEnt->d_name, subdir_ent->d_name); //这里的subdir_ent->d_name 是"cmdline"
			  //              printf("We found the cmdline file:%s\n", cmdline);

			  FILE *fp;
			  fp = fopen(cmdline,"r");
			  if(fp==NULL){
				  fprintf(stderr,"Can't open file %s \n", cmdline);
				  closedir(subdir);
				  closedir(procDir);
				  exit(EXIT_FAILURE);

			  }

			  /*We found the Application under /proc/pid by parsing cmdline file, cmdline has filepath as relative,
			   * using basename get the Application name only
			   */

			  char *data = malloc(512 * sizeof(char));
			  fgets(data,512,fp);
			  const char *cmd = (const char *)basename (data);

			  if(strcmp(cmd,AppName)==0)  // cmdline 中 basename文件名与参数一致
			  {
				  char memfile[1024] = "";
				  printf("Found Command %s,now open mem file.\n", cmd );
				  sprintf(memfile,"/proc/%s/statm",pProcDirEnt->d_name);  //这里的pProcDirEnt->d_name 对应的是进程ID
				  CheckUsage(memfile);
			  }
			  free(data);
			  fclose(fp);
		  }
	  } //while loop to parse directory inside proc directory
	  closedir(subdir);
  } //while parse proc dir

  closedir(procDir);
  return EXIT_SUCCESS;
}
