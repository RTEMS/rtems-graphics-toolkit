/* Added to function as a wrapper from 
 * nano-X rtems_main call and usual main entry-point
 */

extern int main(int argc, char **argv);

extern "C" int rtems_main(int argc, char **argv)
{
  using namespace std;
  return main(argc, argv);
}

