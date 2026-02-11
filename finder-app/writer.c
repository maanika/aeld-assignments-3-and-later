#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char* argv[])
{
	openlog(NULL, 0, LOG_USER);
       
	if (argc != 3) {
		syslog(LOG_ERR, "Invalid number of arguments (%d), expected 2", argc);
		closelog();
		return 1;
	}

	const char* writefile = argv[1];
	const char* writestr = argv[2];

	syslog(LOG_INFO, "Writing \"%s\" to %s", writestr, writefile);

	int fd;
	fd = open(writefile, O_WRONLY | O_CREAT | O_TRUNC, 0755);
	if (fd == -1) {
 		syslog(LOG_ERR, "Failed to open file");
		closelog();
		return 1;
	}


	ssize_t nr;
	nr = write(fd, writestr, strlen(writestr));
	if (nr == -1) {
		syslog(LOG_ERR, "Failed to write to file");
		if (close(fd) == -1) {
			syslog(LOG_ERR, "Failed to close file");
		}
		closelog();
		return 1;
	}

	if (close(fd) == -1) {
		syslog(LOG_ERR, "Failed to close file");
		closelog();
		return 1;
	}
	
	closelog();
	return 0;
}


