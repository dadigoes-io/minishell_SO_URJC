#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "parser.h"

#define SIZE_COMMAND 16
#define PATH_MAX 2048
#define MAX_JOBS_SIZE 16

pid_t *pid_jobs;
int size_jobs;
char* filename_jobs[1024];
char *state_jobs;
int priority_jobs[MAX_JOBS_SIZE];

void handler_chld(int sig){
	if (sig == SIGCHLD){
		int status, found;
		pid_t pid_d;
		found = 0;
		while ((pid_d = waitpid(-1,&status,WNOHANG)) > 0){
			if (kill(pid_d,0) < 0){
				for(int i = 0; i < size_jobs && !found; ++i){
					if (pid_d == pid_jobs[i]){
						state_jobs[i] = 'D';
						found = 1;
					}
				}
			}
		}
	}
}

void insert_job(pid_t pid, char* filename, char state){
	pid_jobs[size_jobs] = pid;
	strcpy(filename_jobs[size_jobs],filename);
	state_jobs[size_jobs] = state;

	int i = 0;
	int pos_j = priority_jobs[0];

	if (state != 'S'){
		while(state_jobs[pos_j] == 'S' && i < size_jobs && pos_j != -1){
			i++;
			pos_j = priority_jobs[i];
		}
	}

	for (int k = size_jobs; k >= i; k--)
		priority_jobs[k+1] = priority_jobs[k];
	priority_jobs[i] = size_jobs;
	size_jobs++;
}

int digits(int n){
	int digits = 1;
	while (n > 10){
		n /= 10;
		digits++;
	}
	return digits;
}

int main(){
	//Declaracion variables
	int i,j,n,status,aux;
	int fd_in,fd_out,fd_err;
	char path[PATH_MAX];
	char buffer[1024];
	char *pos = NULL;
	pid_t pid_w;
	tline* line;
	tcommand* command;
	int umask_msh = 2;

	//Inicialzacion variables jobs
	pid_jobs = NULL;
	size_jobs = 0;
	state_jobs = NULL;
	for(i = 0; i < MAX_JOBS_SIZE; i++)
		priority_jobs[i] = -1;


	//Establecemos signals
	signal(SIGINT, SIG_IGN);
	signal(SIGTSTP, SIG_IGN);
	signal(SIGCHLD, handler_chld);


	//Mostramos prompt por primera vez
	printf("msh> ");

	while (fgets(buffer,1024,stdin) != NULL){
		//Lectura comando
		line = tokenize(buffer);
		command = line->commands;
		n = line->ncommands;

		//Creamos pids del proceso
		pid_t pid[n];
		//Creamos array de pipes
		int p[n-1][2];

		//Eliminamos '\n' del buffer del comando leido
		pos = strchr(buffer,'\n');
		if (pos != NULL)
			*pos = '\0';

		//Reservamos memoria para jobs ejecutados en background
		if (line->background == 1){
			pid_jobs = realloc(pid_jobs, (size_jobs+1)*sizeof(pid_t));
			state_jobs = realloc (state_jobs, (size_jobs+1)*sizeof(char));
			filename_jobs[size_jobs] = malloc(strlen(buffer)+1);
			if (pid_jobs == NULL){
				fprintf(stderr, "Error al reservar memoria para pid_jobs:\n%s\n",strerror(errno));
				exit(1);
			}
			if (state_jobs == NULL){
				fprintf(stderr, "Error al reservar memoria para state_jobs:\n%s\n",strerror(errno));
				exit(1);
			}
			if (filename_jobs[size_jobs] == NULL){
				fprintf(stderr, "Error al reservar memoria para size_jobs:\n%s\n",strerror(errno));
				exit(1);
			}
		}

		//Creamos pipes
		for (i = 0; i < n-1; ++i)
			pipe(p[i]);

		// Ejecutamos los n procesos del job	
		for (i = 0; i < n; ++i){

			if (command[i].filename != NULL){
			// Mandatos externos

				//Realizamos fork
				pid[i] = fork();
				if (pid[i] == 0){

				//Proceso hijo

					//Establecemos signal de los procesos hijos
					signal(SIGINT, SIG_DFL);
					signal(SIGTSTP,SIG_DFL);

					//Redireccionar INPUT en el primer mandato
					if (i == 0 && line->redirect_input != NULL){
						fd_in = open(line->redirect_input,O_RDONLY);
						if (fd_in < 0){
							fprintf(stderr,"Error en open(input,...):\n%s\n",strerror(errno));
							exit(1);
						}
						dup2(fd_in,STDIN_FILENO);
					}

					//Establecemos entrada/salida de los pipes
					if (i > 0)
						// Redireccionamos entrada estándar
						dup2(p[i-1][0],STDIN_FILENO);
					if (i < (n - 1))
						// Redireccionamos salida estándar
						dup2(p[i][1],STDOUT_FILENO);
					if (i == (n - 1)){
					//Redirecionar OUTPUT y ERROR en el ultimo proceso

						//Redireccionar OUTPUT
						if (line->redirect_output != NULL){
							fd_out = open(line->redirect_output,O_WRONLY);
							if (fd_out < 0){
								fprintf(stderr,"%s: Error. %s\n",line->redirect_output,strerror(errno));
								exit(1);
							}
							dup2(fd_out,STDOUT_FILENO);
						
						//Redirecionar ERROR
						}else if (line->redirect_error != NULL){
							fd_err = open(line->redirect_error,O_WRONLY);
							if (fd_err < 0){
								fprintf(stderr,"%s: Error. %s\n",line->redirect_output,strerror(errno));
								exit(1);
							}
							dup2(fd_err,STDERR_FILENO);
						}
					}

					//Cierre de pipes una vez redireccionada la entrada y salida estandar
					for (j = 0; j < n-1; j++){
						close(p[j][0]);
						close(p[j][1]);
					}

					//Ejecutamos comando
					execvp(command[i].argv[0], command[i].argv);
					fprintf(stderr,"Error en execvp.\n%s\n",strerror(errno));
					exit(1);

				}else if (pid[i] > 0){
				//Proceso padre

					//Almacenamos pid del ultimo hijo si es bg
					if (line->background && i == (n-1)){
						insert_job(pid[i],buffer,'R');
					}

				}else{
					fprintf(stderr,"Error fork: \n%s\n",strerror(errno));
					exit(1);
				}

			}else{
			//Mandatos internos

				// cd
				if (strcmp(command[i].argv[0],"cd") == 0){
					if (command[i].argc == 1){
						if (chdir(getenv("HOME")) < 0){
							fprintf(stderr,"Error en chdir(path):\n%s\n",strerror(errno));
						}else{
							if (getcwd(path, PATH_MAX) != NULL)
								printf("%s\n", path);
						}
					}else{
						if (chdir(command[i].argv[1]) < 0){
							fprintf(stderr,"Error en chdir(command...):\n%s\n",strerror(errno));
						}else{
							if (getcwd(path, PATH_MAX) != NULL)
								printf("%s\n", path);
						}
					}

				//jobs
				}else if(strcmp(command[i].argv[0],"jobs") == 0){
					for (i = 0; i < size_jobs; i++){
						if (state_jobs[i] != 'd'){
							printf("[%d]",i+1);
							if (i == priority_jobs[0])
								printf("+");
							else if(i == priority_jobs[1])
								printf("-");
							else
								printf(" ");
						}

						switch(state_jobs[i]){
							case('R'):
								printf("  Running");
								break;
							case('S'):
								printf("  Stopped");
								break;
							case('D'):
								printf("  Done\n");
								state_jobs[i] = 'd';
								break;
							case('d'):
								break;
							default:
								printf("Error jobs: state_job incorrecto\n");
								exit(1);
								break;
						}
						if (state_jobs[i] != 'd')
							printf("\t\t\t%s\n",filename_jobs[i]);
					}
				//bg
				}else if(strcmp(command[i].argv[0],"bg") == 0){
					if (size_jobs > 0){
						if (command[i].argc > 1){
							aux = atoi(command[i].argv[1]) - 1;
							if (aux < size_jobs){
								if (state_jobs[aux] == 'S'){
									printf("[%d]",aux+1);
									if (aux == priority_jobs[0])
										printf("+ ");
									else if(aux == priority_jobs[1])
										printf("- ");
									else
										printf("  ");
									printf("%s\n",filename_jobs[aux]);

									state_jobs[aux] = 'R';
									for (i = aux; i < size_jobs; i++)
										priority_jobs[i] = priority_jobs[i+1];
									priority_jobs[size_jobs-1] = aux;

									kill(pid_jobs[aux], SIGCONT);
								}else{
									fprintf(stderr,"msh: bg: job %d already in background\n",aux);
								}
							}else{
								fprintf(stderr,"msh: bg: %d no such job\n",aux);
							}
						}else if (state_jobs[priority_jobs[0]] == 'S'){
							aux = priority_jobs[0];
							printf("[%d]+ %s\n",aux+1,filename_jobs[aux]);
							state_jobs[aux] = 'R';
							for (i = 0; i < size_jobs; i++)
								priority_jobs[i] = priority_jobs[i+1];
							priority_jobs[size_jobs-1] = aux;

							kill(pid_jobs[aux], SIGCONT);
						}else{
							printf("msh: bg: job %d already in background\n",size_jobs);
						}
					}else{
						printf("msh: bg: current: no such job\n");
					}
				//exit
				}else if (strcmp(command[i].argv[0],"exit") == 0){
					free(pid_jobs);
					free(state_jobs);
					for(i = 0; i < size_jobs; i++)
						free(filename_jobs[i]);
					exit(0);

				//umask
				}else if (strcmp(command[i].argv[0],"umask") == 0){
					if (command[i].argc == 1){
						for (j = 4; j > digits(umask_msh); j--)
							printf("0");

						printf("%d\n",umask_msh);
					}else if(command[i].argc == 2){
						if (command[i].argv[1][0] == '-'){
							printf("msh: umask: %s invalid option\n",command[i].argv[1]);
							printf("umask: usage: umask [mode]\n");
						}else{
							aux = atoi(command[i].argv[1]);
							if (aux >= 0 && aux <= 7777){
								while (aux > 999)
									aux -= 1000;
								if (aux <= 777){
									umask_msh = aux;
									aux = 9999;
									umask(umask_msh);
								}
							}
							if (aux != 9999)
								printf("msh: umask: %s: octal numer out of range\n",command[i].argv[1]);
						}
					}else{
						printf("msh: umask: too many arguments\n");
					}
				}else{
			      	fprintf(stderr,"%s: No se encuentra el mandato\n",buffer);
				}
			}
		}
	//Fin de ejecucion del job
	//Proceso padre (final)
		//Cerramos pipes
		for (i = 0; i < n-1; i++){
			close(p[i][0]);
			close(p[i][1]);
		}
		//Procesos en background
		if (line->background == 1){
			//Mostramos ultimo job ejecutando en bg
			printf("[%d] %d\n",size_jobs,pid_jobs[size_jobs-1]);
			//Mostramos jobs que hayan terminado
			for (i = 0; i < size_jobs - 1; i++){
				if (state_jobs[i] == 'D'){
					printf("[%d]  Done\t\t\t%s\n",i+1,filename_jobs[i]);
					state_jobs[i] = 'd';
				}
			}
		}else{
		//Esperar procesos hijos (fg)
			for (i = 0; i < n; i++){
				pid_w = waitpid(pid[i], &status, WUNTRACED);
				if (pid_w > 0 && WIFSTOPPED(status) && (i == n - 1)){
					// Añadimos procesos hijos detenidos
					pid_jobs = realloc(pid_jobs, (size_jobs+1)*sizeof(pid_t));
					state_jobs = realloc(state_jobs, (size_jobs)+1);
					filename_jobs[size_jobs] = malloc(strlen(buffer)+1);
					if (pid_jobs == NULL || state_jobs == NULL || filename_jobs[size_jobs] == NULL){
						fprintf(stderr,"Error malloc(): Añadir hijos detenidos:\n%s\n",strerror(errno));
						exit(1);
					}
					insert_job(pid_w,buffer,'S');
					printf("\n[%d]+  Stopped\t\t\t%s\n",size_jobs, filename_jobs[size_jobs-1]);
				}
			}
		}
		printf("msh> ");
	}
	printf("\n");
	free(pid_jobs);
	free(state_jobs);
	for (i = 0; i < size_jobs; i++)
		free(filename_jobs[i]);
	return 0;
}
