# ResourceMonitor
Command line resource monitor. Displays disk, network, CPU, RAM, and process bottleneck information.

The purpose of this program is to get an idea of where a system performance bottleneck is coming from, whether that be hard drive related, bandwidth related, or something else. The program attempts to give the name of the process which is most likely to be causing a bottleneck. 

![No graphs to read, only numbers.](sample_output.png)

I have this running all of the time in the corner of my screen so I can always see network and disk activity. I propose shrinking the command prompt text size from 8x12 to 6x8, in the Font tab of the Properties window. 

### Usage

RESOURCEMONITOR [/T seconds] [/L logfile] /TSV /H

 /T	Indicates the time delay between data collection is given, in seconds.
    	Defaults to 1 second. May be a decimal.

 /L	Indicates an output logfile name is given.
    	Warning: No write buffer is used. Use a large [/T seconds].

 /TSV	Tab Separated Values. Disables smart formatting for tabs instead.

 /H	Displays this usage/help text.


#### Data Collected:

 Disk% -- Percent Disk Read/Write Time for the physical disk most in use.
      	Internally calculated for all physical disks and then the highest is
      	displayed to catch a disk-related bottleneck.
      	Hard disk drives are often the cause of a slow computer.

 Download -- Bytes downloaded, summed across all network interfaces.

 Upload -- Bytes uploaded, summed across all network interfaces.

 CPU% -- Percent Processor Usage Time, averaged across all processor cores.

 RAM% -- Percent Physical RAM used.


#### Bottleneck Cause Key:

 CPU:	Indicates CPU bottleneck.
     	Displays the estimated percent CPU time the process used.

 RIO:	Indicates Read-bytes I/O bottleneck.
     	Used as an estimation to determine per-process download bytes.

 WIO:	Indicates Write-bytes I/O bottleneck.
     	Used as an estimation to determine per-process upload bytes.

 TIO:	Indicates Total-bytes I/O bottleneck.
     	Used as an estimation to determine per-process percent disk usage.


#### Data Collection Note:

This program uses the Windows Performance Counters API, which by 
default does not track process IDs (PIDs) along with process names. 
This will cause gaps in the displayed data when a new process is 
created or destroyed, because process names are not unique. To enable 
tracking of PIDs, run this program once as an administrator and the 
setting will be enabled if not already set. Further calls to this 
program will not require administrator rights.


#### Example Usage:

RESOURCEMONITOR

RESOURCEMONITOR /T 3

RESOURCEMONITOR /T 10 /L C:\logfile.txt /TSV
