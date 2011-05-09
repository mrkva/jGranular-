#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"
#include <math.h>
#include "buffer.h"
#include "ext_atomic.h"
#include <stdlib.h>

#define kTableLength 512
#define kMinDuration 2
#define kAmpScale .7


// jGranular~
// based on "yasg~"
// yet another sample granulator
// reads a mono sample from an external buffer
// reads a window from a buffer
// arguments are, in order, waveform buffer, window buffer, low read index (in ms), high read index (in ms), low duration, high duration,max number of layers
// the index for reading a grain is chosen randomly between low read index and high read index
// arguments are read in as a list since max doesns't seem to like that many arguments otherwise
// buffer.h is also needed

typedef struct {
	double incrementSample;
	double incrementAmp;
	double amp;
	double indexSample;
	double startIndex;
	double indexAmp;
	int ampFlag;
}grainData;  // contains data for each grain

// object data structure
typedef struct {
    t_pxobject x_obj;	// header
    grainData *grains;
    double sr; // local sample rate
    long lowRead;
	long highRead;
	float lowDuration;
	float highDuration;
	long numberOfLayers;
	long maxNumberOfLayers;
	double gain;
	
    t_symbol *sampleSym; // symbol for wave buffer name
    t_buffer *sampleBuf; // the wavebuffer (t_buffer defined in buffer.h)
    t_symbol *windowSym; // symbol for the window buffer name
    t_buffer *windowBuf; // symbol for the window buffer
} t_jGranular;
t_class *myClass;			// global variable to contain class
t_symbol *psBuffer;    // to contain symbol buffer~ to make sure buffer named sent to the object is a buffer~



t_int *jGranularPerform(t_int *w);

void jGranularInt(t_jGranular *x, long n);
void jGranularIn1(t_jGranular *x, long n);
void jGranularIn2(t_jGranular *x, long n);
void jGranularIn3(t_jGranular *x, long n);
void jGranularIn4(t_jGranular *x, long n);
void jGranularFloat(t_jGranular *x, double n);
void jGranularDblClick(t_jGranular *x);
void jGranularDsp(t_jGranular *x, t_signal **sp, short *count);
void jGranularAssist(t_jGranular *x, void *b, long m, long a, char *s);
void jGranularSetSample(t_jGranular *x, t_symbol *s);
void jGranularSetWindow(t_jGranular *x, t_symbol *s);
void jGranularMakeHannWindow(t_jGranular *x);
void jGranularFree (t_jGranular *x);
void *jGranularNew(Symbol *s, long ac, Atom *av);
double Alea (double min, double max);
long MsToSample (double ms, double sr);

int main(void) {
	t_class *c;
	c = class_new("jGranular~", (method)jGranularNew, (method)jGranularFree, sizeof(t_jGranular), 0L, 
    	A_GIMME, 0);
    class_addmethod(c, (method)jGranularDsp, "dsp", A_CANT, 0);
    class_addmethod(c, (method)jGranularInt, "int", A_LONG, 0);
	class_addmethod(c, (method)jGranularIn1, "in1", A_LONG, 0);
	class_addmethod(c, (method)jGranularIn2, "in2", A_LONG, 0);
	class_addmethod(c, (method)jGranularIn3, "in3", A_LONG, 0);
	class_addmethod(c, (method)jGranularIn4, "in4", A_LONG, 0);
	class_addmethod(c, (method)jGranularFloat, "ft5", A_FLOAT, 0);
    class_addmethod(c, (method)jGranularAssist, "assist", A_CANT, 0);
    class_addmethod(c, (method)jGranularDblClick, "dblclick", A_CANT, 0);
	class_addmethod(c, (method)jGranularSetSample, "setsample", A_SYM, 0);
	class_addmethod(c, (method)jGranularSetWindow, "setwindow", A_SYM, 0);

    class_dspinit(c);
	class_register(CLASS_BOX, c);
	myClass = c;

    psBuffer = gensym("buffer~"); // make a symbol for buffer~ for later comparison
    return 0;
}

// assist method (for help strings)
void jGranularAssist(t_jGranular *x, void *b, long m, long a, char *s) {
	if (m == ASSIST_OUTLET)
		sprintf(s, "Signal output");
		else
			switch(a) {
				case 0: sprintf(s, "Low index for start point");
					break;
				case 1: sprintf(s, "High index for start point");
					break;
				case 2: sprintf(s, "Low duration");
					break;
				case 3: sprintf(s, "High duration");
					break;
				case 4: sprintf(s, "Number of layers");
					break;
			}		
	
}

void jGranularSetSample(t_jGranular *x, t_symbol *s) {
	t_buffer *b;
	
	x->sampleSym = s; // save symbol
	if ((b = (t_buffer *)(s->s_thing)) && ob_sym(b) == psBuffer) { //save buffer (and check that symbol corresponds to a buffer~
		x->sampleBuf = b;
	} else {  // otherwise give an error message
		object_error((t_object *) x, "no sample buffer~ %s", s->s_name);
		x->sampleBuf = 0;
	}
}



/*void jGranularSetWindow(t_jGranular *x, t_symbol *s) {
	t_buffer *b;
	
	x->windowSym = s; // save symbol
	if ((b = (t_buffer *)(s->s_thing)) && ob_sym(b) == psBuffer) { //save buffer (and check that symbol corresponds to a buffer~
		x->windowBuf = b;
	} else {  // otherwise give an error message
		object_error((t_object *) x, "No window buffer~ %s", s->s_name);
		x->windowBuf = 0;
	}
} */

void jGranularMakeHannWindow(t_jGranular *x) {
	for (int i = 0; i < 2048; i++) {
		double window = 0.5 * (1 - cos(2*PI*i/2047));
		x->windowBuf[i] = window;
	}
}


// method to create a new instance of the object
void *jGranularNew(t_symbol *s, long ac, Atom *av) {
	t_jGranular *x;
	int i;
   
   	x = (t_jGranular *) object_alloc (myClass); 		// create object	
    dsp_setup((t_pxobject *)x,0);	// set up object and 
    								// fix the number of inlets which are signals
	floatin((t_object *) x, 5); // additional inlets
    intin((t_object *)x,4); 
    intin((t_object *)x,3);								
    intin((t_object *)x,2);	
    intin((t_object *)x,1);						
    outlet_new((t_pxobject *)x, "signal"); // add signal outlet
    if (ac != 8) {
    	object_error((t_object *) x, "Not enough arguments");
    	return(x);
    }
    else {
		x->sampleSym = atom_getsymarg(0,ac, av);
		x->windowSym = atom_getsymarg(1, ac, av);
		x->lowRead = MsToSample(atom_getintarg(2, ac, av), sys_getsr());
		x->highRead = MsToSample(atom_getintarg(3, ac, av), sys_getsr());
		x->lowDuration = ((atom_getintarg(4, ac, av) >= kMinDuration)? atom_getintarg(4, ac, av) : kMinDuration) * 0.001; // store duration as seconds
		x->highDuration = ((atom_getintarg(5, ac, av) >= kMinDuration)? atom_getintarg(5, ac, av) : kMinDuration) * 0.001;
		x->numberOfLayers = atom_getintarg(6, ac, av); // initial number of layers is equal to maxNumberOfLayers;				  
		x->maxNumberOfLayers = x->numberOfLayers;
		x->gain = atom_getfloatarg(7, ac, av);
    }
	x->grains =(grainData *) sysmem_newptr(sizeof(grainData) * x->maxNumberOfLayers); 
	
	if (!x->grains) {
		object_post((t_object *) x, "Not enough memory for this many layers");
		return (x);
	}
	for (i = 0; i <  x->maxNumberOfLayers; i++) {
		x->grains[i].amp = 1.0/(x->maxNumberOfLayers * kAmpScale);
		x->grains[i].indexSample = Alea(x->lowRead, x->highRead);
		x->grains[i].indexAmp = 0;
		x->grains[i].ampFlag = 0;
		x->grains[i].incrementSample = 1.0;
		x->grains[i].startIndex = x->grains[i].indexSample;
	}
    return (x);
}


void jGranularInt(t_jGranular *x, long n) {	
	x->lowRead = MsToSample(n, x->sr);
	if (x->lowRead < 0)
		x->lowRead = 0;
}

void jGranularIn1(t_jGranular *x, long n) {	
	x->highRead = MsToSample(n, x->sr);
//	post("highRead %i", x->highRead);
	// check if there is a sample buffer
	// if yes check if the specified upper sample limit is not larger than the number of samples in the buffer
	if (x->sampleBuf && x->highRead > x->sampleBuf->b_frames)
		x->highRead = x->sampleBuf->b_frames - MsToSample(x->highDuration, x->sr); // if yes, set the upper limit to the 
		                                                                           // end of the buffer minus the longest possible
		                                                                           // duration value (assuming that highDuration is larger
		                                                                           // than the lowDuration)
}

void jGranularIn2(t_jGranular *x, long n) {	
	x->lowDuration = ((n >= kMinDuration)? n : kMinDuration) * .001; // note that no duration below kMinDuration can be input
																	// this prevents the patch from blowing up
}

void jGranularIn3(t_jGranular *x, long n) {	
	x->highDuration = ((n >= kMinDuration)? n : kMinDuration) * .001; 
}

void jGranularIn4(t_jGranular *x, long n) {	
	if (n > x->maxNumberOfLayers)  	// if new number of layers is > than maxNumberOfLayers, set to maxNumberOfLayers
		x->numberOfLayers = x->maxNumberOfLayers;
	else if (n < 1)					// if new number of layers is < 1, set to 1
		x->numberOfLayers = 1;
	else
		x->numberOfLayers = n;		// other new number of layers is n 	
}

void jGranularFloat(t_jGranular *x, double n) {
	x->gain = n;
}

void jGranularDblClick(t_jGranular *x) {
	// if the jGranular~ object is double clicked, send the dblclick message to the buffer object
	
	t_buffer *b;
	
	if ((b = (t_buffer *)(x->sampleSym->s_thing)) && ob_sym(b) == psBuffer)
		mess0((struct object *)b,gensym("dblclick"));
}

// dsp method
void jGranularDsp(t_jGranular *x, t_signal **sp, short *count) {	
	double lengthOverSr;
	short i;
	
	jGranularSetSample(x,x->sampleSym);  // setup up link to buffer specified by symbol for wave
	jGranularSetWindow(x,x->windowSym);  // setup up link to buffer specified by symbol for window
	
	x->sr = sp[0]->s_sr; // save local sr for later calculations
	lengthOverSr = kTableLength/x->sr;
	
	for (i = 0; i <  x->maxNumberOfLayers; i++) // calc increments for all grains using local sr
		x->grains[i].incrementAmp = (1./Alea(x->lowDuration, x->highDuration)) * lengthOverSr;
	
	dsp_add(jGranularPerform,3,x,sp[0]->s_vec, sp[0]->s_n);	// add to dsp chain	
			// arguments in this example are:
			// pointer to the perform method
			// the remaining number of arguments
			// pointer to the object
			// output vector
			// vector size
}

// perform method
t_int *jGranularPerform(t_int *w) {
	int n;
	t_jGranular *x;
	float  *out,*sampleTable,*windowTable,amp,outValue;
	double lengthOverSr, gain;
	t_buffer *sample,*window;
	grainData *grains;
	long numberOfLayers,lowRead,highRead;
	short i,j;
	
	x = (t_jGranular *) w[1]; 	//first argument is a pointer to the object 
	out = (float *)(w[2]); 	// second argument is a pointer to the output vector
	n = (int)(w[3]);		// third argument is the vector size
	
	if (x->x_obj.z_disabled) 
			goto out;
	sample = x->sampleBuf;
	if (!sample)  // if no buffer is available
		goto zero;
	ATOMIC_INCREMENT((int *) &sample->b_inuse);
	if (!sample->b_valid){  //if buffer is not valid (for some reason)
		ATOMIC_DECREMENT((int *)&sample->b_inuse);
		goto zero;
	}
	sampleTable = sample->b_samples; // assign the pointer to the samples (values)
	
	window = x->windowBuf;
	if (!window)  // if no buffer is available
		goto zero;
	ATOMIC_INCREMENT((int *) &window->b_inuse);
	if (!window->b_valid) {  //if buffer is not valid (for some reason)
		ATOMIC_DECREMENT((int *)&window->b_inuse);
		goto zero;
	}
	windowTable = window->b_samples; // assign the pointer to the samples (values)
	
	
	grains = x->grains;
	numberOfLayers = x->numberOfLayers;
	lengthOverSr = kTableLength/x->sr;
	lowRead = x->lowRead;
	highRead = x->highRead;
	gain = x->gain;

	for( i=0, outValue=0; i<n; i++, outValue=0) {	
		for (j = 0; j < numberOfLayers; j++) {
			amp = grains[j].amp * windowTable[(int) grains[j].indexAmp];
			grains[j].indexAmp += grains[j].incrementAmp;
			while (grains[j].indexAmp >= kTableLength) {
				grains[j].ampFlag = 1;
				grains[j].indexAmp -= kTableLength;
			}
	
			outValue += amp * sampleTable[(long) grains[j].indexSample];
			grains[j].indexSample += grains[j].incrementSample;
			if (grains[j].indexSample >= highRead)
				grains[j].indexSample = grains[j].startIndex;
	
			if (grains[j].ampFlag) {
				grains[j].incrementAmp = (1./ Alea(x->lowDuration, x->highDuration)) * lengthOverSr;
				grains[j].ampFlag = 0;
				grains[j].indexSample = Alea (lowRead, highRead);
				grains[j].startIndex = grains[j].indexSample;
			}
		}

		*out++ = outValue * gain;
	}
	ATOMIC_DECREMENT((int *) &sample->b_inuse);
	ATOMIC_DECREMENT((int *) &window->b_inuse);
	return (w+4);			// return a pointer 1 past the last argument called from w
zero:
	while (n--) 
		*++out = 0.; // fill output buffer with zeros if there was a problem
	return (w+4);
out:
	return(w+4);			
}		


void jGranularFree(t_jGranular *x) {
	if (x->grains) // if there is a pointer to grains
		sysmem_freeptr(x->grains); //then free the storage
	dsp_free((t_pxobject *)x); //call the standard msp free routine
}

// random in the range min-max
double Alea (double min, double max) {
	return ((double) rand()/ RAND_MAX) * (max - min) + min;
}

long MsToSample (double ms, double sr) {
	return  sr * .001 * ms;
}
