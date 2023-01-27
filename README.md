# My-pipeline-implementation

This program supports a 10 stage pipline that works just like the traditional unix/linux pipeline. First build with the makefile on the command line, then the usage is "./runpipeline <prog1> -- prog<2> ...

Uses fork() to create child processses that use exec() to run the program. Supports chaining up to 10 programs.
