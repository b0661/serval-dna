#include <dlfcn.h>
#include <stdio.h>

int main(int argc,char **argv)
{
 void *h = dlopen("/data/data/org.servalproject/lib/libdnalib.so",RTLD_LAZY);
 int (*dnamain)(int,char **) = dlsym(h,"main");
 if (!dnamain) return fprintf(stderr,"Could not load libdnalib.so\n");
 return (*dnamain)(argc,argv);

}
