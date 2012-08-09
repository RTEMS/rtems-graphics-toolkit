//#ifndef RTEMS_MAIN_H
//#define RTEMS_MAIN_H

//transform call to main to call to rtems_main
//#define main(x,y) rtems_main(x,y)
//#define main(x)   rtems_main(x)
//#define main()    rtems_main()

//extern "C" int rtems_main(int argc, char **argv)
//{
//  main(argc, argv);
//}
//
//
//#endif
//extern "C" 
extern int main(int argc, char **argv);


extern "C" int rtems_main(int argc, char **argv)
{
  using namespace std;
  return main(argc, argv);
}

