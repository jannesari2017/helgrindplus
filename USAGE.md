== Helgrind+ User Manual ==

''' Additional commands provided in Helgrind+  3.4.1: (based on Valgrind 3.4.1) ''' 

----
=== Helgrind+ can be used with 3 different memory state models (MSM switches) : ===

	* valgrind --tool=helgrind : using Helgrind+ with standard MSM
	* valgrind --tool=helgrind-ukas: using Helgrind+ with MSM-short version (adequate for short-running application)
	* valgrind --tool=helgrind-ukal : using Helgrind+ with MSM-long version (adequate for long-running application)
	* valgrind --tool=helgrind-ukas-nolib : using Helgrind+ with MSM-short version and turning off the Pthread library information (no library information) 
	* valgrind --tool=helgrind-ukal-nolib : using Helgrind+ with MSM-long version and turning off the Pthread library information (no library information)



----

=== New command line options for Lost Signal Detector (LSD): ===

	*  --lsd=no|yes|wr : could be used for all 3 versions of MSMs :
			* --lsd=no : no ahead translation
			* --lsd=yes : with ahead translation
			* --lsd=wr : with Write/Read - relation additional to ahead translation (most accurate)
			
			* Example: valgrind --tool=helgrind-ukas  --lsd=wr --suppressions=helgrind.supp  date
				* running Helgrind+ based on MSM-short with lost signal detection and Write/Read - relation

 	*  --verbose-lostsig : could be used for debugging purposes

----

=== Control flow analysis now comes with spinning read loop detection: ===

	* use command line option "--verbose-cfg" to dump details of detected spin reads
	* use "--cfg=N" to set the maximum of basic blocks a spin read loop may span
		- if it's set too low, spin reads may be missed
		- if it's set too high, there may be false positives
		- the value of 3 shows good results for some x86 programs

                       * Example: valgrind --tool=helgrind-ukas-nolib --cfg=3  date
				* running Helgrind+ based on MSM-short with spinning read loop detection. Helgrind+ works as universal race detector with no library information.

----

=== Helgrind+ works as a hybrid universal race detector (including lockset analysis): ===
This option can be only used together with the option for spinning read loop detection "--cfg=N".

	* use command line option "--cfg-mutex=yes" to enable lock detection. It uses the hybrid algorithm in universal race-detector mode and performs the lockset analysis in addition to happens-before analysis.
	* use command line option "--cfg-mutex=no" to disable lock detection.

----

=== Option to turn off spinning read loop detection within Pthread synchronization primitives (and Helgrind+ internals): ===		

	*  --ignore-pthread-spins
        *  You may also use "--cfg=0"

----	
=== Counting the number of calls to synchronization primitives: ===

	*  -v : pthead_mutex, pthread_cond_wait/signal and pthread_barrier_wait
----