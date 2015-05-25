#include <Magick++.h> 
#include <string> 
#include <iostream> 

using namespace std; 
using namespace Magick; 

int main(int argc, char const *argv[])
{
    InitializeMagick(*argv);

    Magick::Image img(argv[1]); 
    std::cout << img.magick() << "\n"; 

    Magick::Geometry new_size("230x230");
    Magick::Geometry old_size = img.size();
    if(old_size.height()>new_size.height() || old_size.width()>new_size.width())
    {   
        img.sample(new_size);
        std::cout << "230x230 \n";
    }

    img.write(img.magick() + std::string(":out.png"));

    return 0;
}

