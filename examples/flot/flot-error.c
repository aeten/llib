#include <stdio.h>
#include <llib/table.h>
#include <llib/flot.h>

int main()
{
    Flot *P = flot_new("caption", "Data with Errors","xaxis.min",VF(0.5),"xaxis.max",VF(4.5));
    
    double **data = array_new(double*,4);
    data[0] = farr_4(1,2,0.2,0);
    data[1] = farr_4(2,2.2,0.2,0);
    data[2] = farr_4(3,3.5,0.4,0);
    data[3] = farr_4(4,3,0.2,0);
   
    flot_series_new(P,(farr_t)data, NULL,FlotPoints,"label","my data",
        "points.radius",VF(5),
        "points.fillColor","red",
        "points.errorbars","y",
        "points.yerr.show",True,
        "points.yerr.lowerCap","-",
        "points.yerr.upperCap","-"
    );
    
    flot_render("errors");
    return 0;
}

