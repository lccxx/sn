#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <security/pam_appl.h>

#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAXLINE 1024

char * name;

int debug = 0;

void usage (char * err) {
	if (err!=NULL) fprintf(stderr, "Error: %s\n", err);
	fprintf(stderr,"Usage:\n\t%s [-p <name>]\n\n\t-p <name>\tuse <name> as the application name for PAM authentication (default - sn)\n-d\tSend debugging information to syslog\n",name);
	exit(1);
}

char * get_line(char * line, int size) {
	char * res = fgets(line, size, stdin);
	if (res!=NULL) {
		if (debug) syslog(LOG_DEBUG," <<< %s",line);
		if (!strncasecmp(line,"QUIT",4)) {
			if (debug) syslog(LOG_DEBUG," >>> 205 .");
			fputs("205 .\r\n",stdout);
			exit(0);
		}
	}
	return res;
}

int nntp_conv (int num_msg, const struct pam_message **msg, struct pam_response **resp, void * app_data) {
	int i,j;
	char line[MAXLINE];
	char * res=NULL;

	for (i=0;i<num_msg;i++) {
		switch (msg[i]->msg_style) {
			case PAM_PROMPT_ECHO_ON:
				if (debug) syslog(LOG_DEBUG," >>> 480 Authentication required - %s\n",msg[i]->msg);
				printf("480 Authentication required - %s\r\n",msg[i]->msg);
				if (get_line(line,MAXLINE) == NULL) return PAM_CONV_ERR;
				if (strncasecmp(line,"AUTHINFO USER ",14)) res=NULL;
					else res=strdup(line+14);
				break;
			case PAM_PROMPT_ECHO_OFF:
				if (debug) syslog(LOG_DEBUG," >>> 381 Password required - %s\n",msg[i]->msg);
				printf("381 Password required - %s\r\n",msg[i]->msg);
				if (get_line(line,MAXLINE) == NULL) return PAM_CONV_ERR;
				if (strncasecmp(line,"AUTHINFO PASS ",14)) res=NULL;
					else res=strdup(line+14);
				break;
			case PAM_TEXT_INFO:
				break;
			case PAM_ERROR_MSG:
				if (debug) syslog(LOG_DEBUG," >>> 481 Authentication failure - %s\n",msg[i]->msg);
				printf("481 Authentication failure - %s\r\n",msg[i]->msg);
			default:
				return PAM_CONV_ERR;

		}
		if (res!=NULL) for(j=strlen(res)-1; ((j>=0)&&((res[j]=='\r')||(res[j]=='\n')||(res[j]==' ')||(res[j]=='\t'))); res[j--]=0);
		resp[i]=(struct pam_response *) malloc(sizeof (struct pam_response));
		resp[i]->resp=res;
		resp[i]->resp_retcode=0;
	}

	return PAM_SUCCESS;
}

void setrhost (pam_handle_t *pamh) {
	struct sockaddr_in from;
	socklen_t fromlen=sizeof (from);
	char * host;
	struct hostent *hp;

	if (getpeername(0, (struct sockaddr *)&from, &fromlen) < 0) return;
	hp = gethostbyaddr((char *)(&from.sin_addr), sizeof (struct in_addr), from.sin_family);
	if (hp!=NULL) host = hp->h_name; else host = inet_ntoa(from.sin_addr);

	pam_set_item(pamh,PAM_RHOST,host);

}

int main (int argc, char * const argv[]) {
	pam_handle_t *pamh = NULL;
	char * pam_name = "sn";
	char path[sizeof(BINDIR)+MAXLINE];
	struct pam_conv conv = { nntp_conv, NULL };
	int i;

	name = argv[0];
	for (i=1; i< argc; i++) {
		if ((! strcmp(argv[i],"-p"))&&(i+1<argc))
			pam_name = argv[++i];
		else if (! strcmp(argv[i],"-d"))
			debug=1;
		else
			usage (NULL);
	}

	openlog ("snauth", LOG_PID, LOG_AUTHPRIV);

	setlinebuf(stdout);
	if (debug) syslog(LOG_DEBUG," >>> 200 Hi, Posting is OK, but you need to authenticate");
	fputs("200 Hi, Posting is OK, but you need to authenticate\r\n",stdout);
	if (get_line(path,sizeof(path)) == NULL) {
		syslog (LOG_ERR, "Never got the first message");
		exit(1);
	}
	if (pam_start(pam_name, NULL, &conv, &pamh) != PAM_SUCCESS) {
		syslog (LOG_ERR, "PAM initialization failed");
		exit(1);
	}
	setrhost (pamh);

	if (pam_authenticate(pamh, PAM_DISALLOW_NULL_AUTHTOK) != PAM_SUCCESS) {
		 if (debug) syslog(LOG_DEBUG," >>> 481 Authentication failure");
		 fputs("481 Authentication failure\r\n",stdout);
		 exit(1);
	}

	pam_end(pamh, PAM_SUCCESS);
	closelog();

	setenv("POSTING_OK","yes",1);
	if (debug) syslog(LOG_DEBUG," >>> 281 Auth OK");
	fputs("281 Auth OK\r\n",stdout);
	strcpy(path,BINDIR);
	strcat(path,":/bin:/usr/bin");
	setenv("PATH",path,1);
	strcpy(path,BINDIR);
	strcat(path,"/snntpd");
	if (debug) return(execl(path,path,"-S","-d","-d","-d","logger","-p","news.info",NULL));
	else return(execl(path,path,"-S","logger","-p","news.info",NULL));
}
