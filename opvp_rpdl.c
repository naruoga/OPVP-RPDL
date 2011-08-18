/*

Copyright (c) 2003-2006, AXE, Inc.  All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include	<stdio.h>
#include	<stdlib.h>
#include	<stdarg.h>
#include	<time.h>
#include	<sys/time.h>
#include	<string.h>
#include	"opvp_common.h"
#include	"opvp_rpdl.h"

/* macros */
#define InitBrush       { \
                                OPVP_cspaceStandardRGB, \
                                {255,255,255,255}, \
                                0, \
                                0, \
                                NULL \
                        }


/* definition */
typedef	struct {
	OPVP_Point	pos;
	OPVP_Brush	sCol;
	OPVP_Brush	fCol;
	OPVP_Brush	bCol;
	OPVP_LineStyle	lStyle;
	OPVP_LineCap	lCap;
	OPVP_LineJoin	lJoin;
	OPVP_Fix	mLimit;
	OPVP_PaintMode	pMode;
} GS;
typedef	struct	_GS_stack {
	struct	_GS_stack	*prev;
	struct	_GS_stack	*next;
	GS			gs;
} GS_stack;

/* global variables */
int	errorno = OPVP_OK;

/* private variables */
static	int		outputStream = -1;
static	int		pContext = -1;
static	OPVP_CTM	ctm = {1,0,0,1,0,0};
static	OPVP_api_procs	apiList;
static	OPVP_ColorSpace	csList[3] =
			{
				OPVP_cspaceStandardRGB,
				OPVP_cspaceDeviceGray,
				OPVP_cspaceBW
			};
static	OPVP_ColorSpace	colorSpace = OPVP_cspaceStandardRGB;
static	OPVP_ROP	ropList[5] =
			{
				OPVP_ropPset,
				OPVP_ropPreset,
				OPVP_ropOr,
				OPVP_ropAnd,
				OPVP_ropXor
			};
static	OPVP_ROP	rasterOp = OPVP_ropPset;
static	OPVP_FillMode	fill = OPVP_fillModeWinding;
static	float		alphaConst = 0;
static	OPVP_Fix	lineWidth = 0;
static	int		dashNum = 0;
static	OPVP_Fix	*dash = (OPVP_Fix*)NULL;
static	OPVP_Fix	dashOffset = 0;
static	OPVP_ClipRule	clip = OPVP_clipRuleEvenOdd;
static	GS		currentGS = {
				     {0,0},InitBrush,InitBrush,InitBrush,
				     OPVP_lineStyleSolid,OPVP_lineCapButt,
				     OPVP_lineJoinMiter,0,OPVP_paintModeOpaque
				    };
static	GS_stack	*GSstack = NULL;

static
void SetApiList(void);


#define	BUF_SIZE	1024

static
void Output(char *fmt,...)
{
#ifdef	DEBUG_PRINT
	va_list	ap;
	int	d;
	char	c, *s;
	float	f;
	double	lf = 0.0;
	FILE	*fp = stderr;

	va_start(ap, fmt);

	while (*fmt) {
		switch (*fmt) {
			case '%':
				switch (*(++fmt)) {
					case 'c':
						c = (char)va_arg(ap, int);
						fprintf(fp, "%c", c);
						break;
					case 's':
						s = va_arg(ap, char *);
						fprintf(fp, "%s", s);
						break;
					case 'd':
						d = va_arg(ap, int);
						fprintf(fp, "%d", d);
						break;
					case 'f':
						f = (float)va_arg(ap, double);
						fprintf(fp, "%f", f);
						break;
					case 'F':
						d = va_arg(ap, int);
						f = (float)d/(float)OPVP_FIX_FRACT_DENOM;
						fprintf(fp, "%f", f);
						break;
					case 'x':
						d = va_arg(ap, int);
						fprintf(fp, "%x", d);
						break;
					case 'X':
						d = va_arg(ap, int);
						fprintf(fp, "%X", d);
						break;
					default:
						fprintf(fp, "%%");
						break;
				}
				break;
			default:
				fputc((int)*fmt, fp);
				break;
		}
		fmt++;
	}

	va_end(ap);
#endif
}

/*
 * ------------------------------------------------------------------------
 * Creating and Managing Print Contexts
 * ------------------------------------------------------------------------
 */

/*
 * OpenPrinter
 */
int	OpenPrinter(
	int		outputFD,
	char		*printerModel,
	int		*nApiEntry,
	OPVP_api_procs	**apiEntry
	)
{
	errorno = OPVP_OK;

	if (pContext != -1) {
		errorno = OPVP_BADREQUEST;
		return -1;
	}

	pContext = 1;
	OPVP_i2Fix(1,lineWidth);

	outputStream = outputFD;

	SetApiList();
	*nApiEntry = sizeof(apiList) / sizeof(int(*)());
	*apiEntry = &apiList;

	Output("OpenPrinter:outputFD=%d",outputFD);
	Output(":printerModel=%s",printerModel);
	Output(":nApiEntry=%d",*nApiEntry);
	Output(":printerContext=%x\n",pContext);

	return pContext;
}

/*
 * ClosePrinter
 */
static
int	ClosePrinter(
	int		printerContext
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("ClosePrinter:printerContext=%d\n",printerContext);

	outputStream = -1;
	pContext = -1;

	return OPVP_OK;
}

/*
 * ------------------------------------------------------------------------
 * Job Control Operations
 * ------------------------------------------------------------------------
 */

/*
 * StartJob
 */
static
int	StartJob(
	int		printerContext,
	char		*jobInfo
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("StartJob:printerContext=%d",printerContext);
	Output(":jobInfo=%s\n",jobInfo);

	return OPVP_OK;
}

/*
 * EndJob
 */
static
int	EndJob(
	int		printerContext
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("EndJob:printerContext=%d\n",printerContext);

	return OPVP_OK;
}

/*
 * StartDoc
 */
static
int	StartDoc(
	int		printerContext,
	char		*docInfo
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("StartDoc:printerContext=%d",printerContext);
	Output(":docInfo=%s\n",docInfo);

	return OPVP_OK;
}

/*
 * EndDoc
 */
static
int	EndDoc(
	int		printerContext
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("EndDoc:printerContext=%d\n",printerContext);

	return OPVP_OK;
}

/*
 * StartPage
 */
static
int	StartPage(
	int		printerContext,
	char		*pageInfo
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("StartPage:printerContext=%d",printerContext);
	Output(":pageInfo=%s\n",pageInfo);

	return OPVP_OK;
}

/*
 * EndPage
 */
static
int	EndPage(
	int		printerContext
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("EndPage:printerContext=%d\n",printerContext);

	return OPVP_OK;
}

static
int     QueryDeviceCapability(
	int		printerContext,
	int		queryflag,
	int		buflen,
	char		*infoBuf
	)
{
	char *dummyInfo = "deviceCapability";

	errorno = OPVP_OK;
	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("QueryDeviceCapability:printerContext=%d\n",
	   printerContext);
	Output(":queryflag=%d\n",queryflag);
	Output(":buflen=%d\n",buflen);

	if (infoBuf != NULL) {
	    int rlen = strlen(dummyInfo);

	    if (rlen > buflen-1) rlen = buflen-1;
	    strncpy(infoBuf,dummyInfo,rlen+1);
	}

	return OPVP_OK;
}

static
int     QueryDeviceInfo(
	int		printerContext,
	int		queryflag,
	int		buflen,
	char		*infoBuf
	)
{
	char *dummyInfo = "deviceInfo";

	errorno = OPVP_OK;
	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("QueryDeviceInfo:printerContext=%d\n",
	   printerContext);
	Output(":queryflag=%d\n",queryflag);
	Output(":buflen=%d\n",buflen);

	if (infoBuf != NULL) {
	    int rlen = strlen(dummyInfo);

	    if (rlen > buflen-1) rlen = buflen-1;
	    strncpy(infoBuf,dummyInfo,rlen+1);
	}

	return OPVP_OK;
}

/*
 * ------------------------------------------------------------------------
 * Graphics State Object Operations
 * ------------------------------------------------------------------------
 */

/*
 * ResetCTM
 */
static
int	ResetCTM(
	int		printerContext
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("ResetCTM\n");

	ctm.a = 1;
	ctm.b = 0;
	ctm.c = 0;
	ctm.d = 1;
	ctm.e = 0;
	ctm.f = 0;

	return OPVP_OK;
}

/*
 * SetCTM
 */
static
int	SetCTM(
	int		printerContext,
	OPVP_CTM	*pCTM
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}
	if (!pCTM) {
		errorno = OPVP_PARAMERROR;
		return -1;
	}

	Output("SetCTM:CTM={%f,",pCTM->a);
	Output("%f,",pCTM->b);
	Output("%f,",pCTM->c);
	Output("%f,",pCTM->d);
	Output("%f,",pCTM->e);
	Output("%f}\n",pCTM->f);

	ctm = *pCTM;

	return OPVP_OK;
}

/*
 * GetCTM
 */
static
int	GetCTM(
	int		printerContext,
	OPVP_CTM	*pCTM
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}
	if (!pCTM) {
		errorno = OPVP_PARAMERROR;
		return -1;
	}

	Output("GetCTM:CTM={%f,",ctm.a);
	Output("%f,",ctm.b);
	Output("%f,",ctm.c);
	Output("%f,",ctm.d);
	Output("%f,",ctm.e);
	Output("%f}\n",ctm.f);

	*pCTM = ctm;

	return OPVP_OK;
}

/*
 * InitGS
 */
static
int	InitGS(
	int		printerContext
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("InitGS\n");

	return OPVP_OK;
}

/*
 * SaveGS
 */
static
int	SaveGS(
	int		printerContext
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("SaveGS\n");

	if (!GSstack) {
		GSstack = calloc(sizeof(GS_stack),1);
		GSstack->prev = NULL;
		GSstack->next = NULL;
	} else {
		GSstack->next = calloc(sizeof(GS_stack),1);
		GSstack->next->prev = GSstack;
		GSstack->next->next = NULL;
		GSstack = GSstack->next;
	}
	GSstack->gs = currentGS;

	return OPVP_OK;
}

/*
 * RestoreGS
 */
static
int	RestoreGS(
	int		printerContext
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("RestoreGS\n");

	if (!GSstack) {
		currentGS = GSstack->gs;
		if (GSstack->prev) {
			GSstack = GSstack->prev;
			free(GSstack->next);
			GSstack->next = NULL;
		} else {
			free(GSstack);
			GSstack = NULL;
		}
	}

	return OPVP_OK;
}

/*
 * QueryColorSpace
 */
static
int	QueryColorSpace(
	int		printerContext,
	OPVP_ColorSpace	*pcspace,
	int		*pnum
	)
{
	int		i;
	int		n;

	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}
	if (!pcspace) {
		errorno = OPVP_PARAMERROR;
		return -1;
	}
	if (!pnum) {
		errorno = OPVP_PARAMERROR;
		return -1;
	}

	n = sizeof(csList)/sizeof(OPVP_ColorSpace);

	Output("QueryColorSpace:");

	for (i=0;i<*pnum;i++) {
		if (i >= n) {
			break;
		} else {
			Output("%d ",csList[i]);
			pcspace[i] = csList[i];
		}
	}
	Output("\n");
	*pnum = n;

	return OPVP_OK;
}

/*
 * SetColorSpace
 */
static
int	SetColorSpace(
	int		printerContext,
	OPVP_ColorSpace	cspace
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("SetColorSpace:cspace=%d\n",cspace);

	colorSpace = cspace;

	return OPVP_OK;
}

/*
 * GetColorSpace
 */
static
int	GetColorSpace(
	int		printerContext,
	OPVP_ColorSpace	*pcspace
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}
	if (!pcspace) {
		errorno = OPVP_PARAMERROR;
		return -1;
	}

	Output("GetColorSpace:cspace=%d\n",colorSpace);

	*pcspace = colorSpace;

	return OPVP_OK;
}

/*
 * QueryROP
 */
static
int	QueryROP(
	int		printerContext,
	int		*pnum,
	int		*prop
	)
{
	int		i;
	int		n;

	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}
	if (!pnum) {
		errorno = OPVP_PARAMERROR;
		return -1;
	}
	if (!prop) {
		errorno = OPVP_PARAMERROR;
		return -1;
	}

	n = sizeof(ropList)/sizeof(OPVP_ROP);

	Output("QueryROP:");

	for (i=0;i<*pnum;i++) {
		if (i >= n) {
			break;
		} else {
			Output("%d ",ropList[i]);
			prop[i] = ropList[i];
		}
	}
	Output("\n");
	*pnum = n;

	return OPVP_OK;
}

/*
 * SetROP
 */
static
int	SetROP(
	int		printerContext,
	int		rop
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("SetROP:rop=%d\n",rop);

	rasterOp = rop;

	return OPVP_OK;
}

/*
 * GetROP
 */
static
int	GetROP(
	int		printerContext,
	int		*prop
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}
	if (!prop) {
		errorno = OPVP_PARAMERROR;
		return -1;
	}

	Output("GetROP:rop=%d\n",rasterOp);

	*prop = rasterOp;

	return OPVP_OK;
}

/*
 * SetFillMode
 */
static
int	SetFillMode(
	int		printerContext,
	OPVP_FillMode	fillmode
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("SetFillMode:fillmode=%d\n",fillmode);

	fill = fillmode;

	return OPVP_OK;
}

/*
 * GetFillMode
 */
static
int	GetFillMode(
	int		printerContext,
	OPVP_FillMode	*pfillmode
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}
	if (!pfillmode) {
		errorno = OPVP_PARAMERROR;
		return -1;
	}

	Output("GetFillMode:fillmode=%d\n",fill);

	*pfillmode = fill;

	return OPVP_OK;
}

/*
 * SetAlphaConstant
 */
static
int	SetAlphaConstant(
	int		printerContext,
	float		alpha
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("SetAlphaConstant:alpha=%f\n",alpha);

	alphaConst = alpha;

	return OPVP_OK;
}

/*
 * GetAlphaConstant
 */
static
int	GetAlphaConstant(
	int		printerContext,
	float		*palpha
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}
	if (!palpha) {
		errorno = OPVP_PARAMERROR;
		return -1;
	}

	Output("GetAlphaConstant:alpha=%f\n",alphaConst);

	*palpha = alphaConst;

	return OPVP_OK;
}

/*
 * SetLineWidth
 */
static
int	SetLineWidth(
	int		printerContext,
	OPVP_Fix	width
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("SetLineWidth:width=%F\n",width);

	lineWidth = width;

	return OPVP_OK;
}

/*
 * GetLineWidth
 */
static
int	GetLineWidth(
	int		printerContext,
	OPVP_Fix	*pwidth
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}
	if (!pwidth) {
		errorno = OPVP_PARAMERROR;
		return -1;
	}

	Output("GetLineWidth:width=%F\n",lineWidth);

	*pwidth = lineWidth;

	return OPVP_OK;
}

/*
 * SetLineDash
 */
static
int	SetLineDash(
	int		printerContext,
	OPVP_Fix	pdash[],
	int		num
	)
{

	errorno = OPVP_OK;
	int		i;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}
	if (num<0) {
		errorno = OPVP_PARAMERROR;
		return -1;
	}
	if ((num>0)&&(!pdash)) {
		errorno = OPVP_PARAMERROR;
		return -1;
	}

	if (num) {
		if (dash) {
			dash = realloc(dash,(sizeof(OPVP_Fix))*num);
		} else {
			dash = malloc((sizeof(OPVP_Fix))*num);
		}
		if (!dash) {
			errorno = OPVP_FATALERROR;
			return -1;
		}
	} else {
		if (dash) {
			free(dash);
			dash = (OPVP_Fix*)NULL;
		}
	}

	Output("SetLineDash:");

	if (dash) {
		for (i=0;i<num;i++) {
			Output("%F ",pdash[i]);
			dash[i] = pdash[i];
		}
	}
	Output("\n");

	dashNum = num;

	return OPVP_OK;
}

/*
 * GetLineDash
 */
static
int	GetLineDash(
	int		printerContext,
	OPVP_Fix	pdash[],
	int		*pnum
	)
{
	errorno = OPVP_OK;
	int		i;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}
	if (!pdash) {
		errorno = OPVP_PARAMERROR;
		return -1;
	}
	if (!pnum) {
		errorno = OPVP_PARAMERROR;
		return -1;
	}

	Output("GetLineDash:");

	for (i=0;i<*pnum;i++) {
		if (i >= dashNum) {
			break;
		} else {
			Output("%F ",dash[i]);
			pdash[i] = dash[i];
		}
	}
	Output("\n");
	*pnum = dashNum;

	return OPVP_OK;
}

/*
 * SetLineDashOffset
 */
static
int	SetLineDashOffset(
	int		printerContext,
	OPVP_Fix	offset
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("SetLineDashOffset:offset=%F\n",offset);

	dashOffset = offset;

	return OPVP_OK;
}

/*
 * GetLineDashOffset
 */
static
int	GetLineDashOffset(
	int		printerContext,
	OPVP_Fix	*poffset
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}
	if (!poffset) {
		errorno = OPVP_PARAMERROR;
		return -1;
	}

	Output("GetLineDashOffset:offset=%F\n",dashOffset);

	*poffset = dashOffset;

	return OPVP_OK;
}

/*
 * SetLineStyle
 */
static
int	SetLineStyle(
	int		printerContext,
	OPVP_LineStyle	linestyle
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("SetLineStyle:linestyle=%d\n",linestyle);

	currentGS.lStyle = linestyle;

	return OPVP_OK;
}

/*
 * GetLineStyle
 */
static
int	GetLineStyle(
	int		printerContext,
	OPVP_LineStyle	*plinestyle
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}
	if (!plinestyle) {
		errorno = OPVP_PARAMERROR;
		return -1;
	}

	Output("SetLineStyle:linestyle=%d\n",currentGS.lStyle);

	*plinestyle = currentGS.lStyle;

	return OPVP_OK;
}

/*
 * SetLineCap
 */
static
int	SetLineCap(
	int		printerContext,
	OPVP_LineCap	linecap
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("SetLineCap:linecap=%d\n",linecap);

	currentGS.lCap = linecap;

	return OPVP_OK;
}

/*
 * GetLineCap
 */
static
int	GetLineCap(
	int		printerContext,
	OPVP_LineCap	*plinecap
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}
	if (!plinecap) {
		errorno = OPVP_PARAMERROR;
		return -1;
	}

	Output("GetLineCap:linecap=%d\n",currentGS.lCap);

	*plinecap = currentGS.lCap;

	return OPVP_OK;
}

/*
 * SetLineJoin
 */
static
int	SetLineJoin(
	int		printerContext,
	OPVP_LineJoin	linejoin
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("SetLineJoin:linejoin=%d\n",linejoin);

	currentGS.lJoin = linejoin;

	return OPVP_OK;
}

/*
 * GetLineJoin
 */
static
int	GetLineJoin(
	int		printerContext,
	OPVP_LineJoin	*plinejoin
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}
	if (!plinejoin) {
		errorno = OPVP_PARAMERROR;
		return -1;
	}

	Output("GetLineJoin:linejoin=%d\n",currentGS.lJoin);

	*plinejoin = currentGS.lJoin;

	return OPVP_OK;
}

/*
 * SetMiterLimit
 */
static
int	SetMiterLimit(
	int		printerContext,
	OPVP_Fix	miterlimit
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("SetMiterLimit:miterlimit=%d\n",miterlimit);

	currentGS.mLimit = miterlimit;

	return OPVP_OK;
}

/*
 * GetMiterLimit
 */
static
int	GetMiterLimit(
	int		printerContext,
	OPVP_Fix	*pmiterlimit
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}
	if (!pmiterlimit) {
		errorno = OPVP_PARAMERROR;
		return -1;
	}

	Output("GetMiterLimit:miterlimit=%d\n",currentGS.mLimit);

	*pmiterlimit = currentGS.mLimit;

	return OPVP_OK;
}

/*
 * SetPaintMode
 */
static
int	SetPaintMode(
	int		printerContext,
	OPVP_PaintMode	paintmode
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("SetPaintMode:paintmode=%d\n",paintmode);

	currentGS.pMode = paintmode;

	return OPVP_OK;
}

/*
 * GetPaintMode
 */
static
int	GetPaintMode(
	int		printerContext,
	OPVP_PaintMode	*ppaintmode
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}
	if (!ppaintmode) {
		errorno = OPVP_PARAMERROR;
		return -1;
	}

	Output("GetPaintMode:paintmode=%d\n",currentGS.pMode);

	*ppaintmode = currentGS.pMode;

	return OPVP_OK;
}

/*
 * SetStrokeColor
 */
static
int	SetStrokeColor(
	int		printerContext,
	OPVP_Brush	*brush
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}
	if (!brush) {
		errorno = OPVP_PARAMERROR;
		return -1;
	}

	Output("SetStrokeColor:");
	if (brush->pbrush) {
		Output("BRUSH:");
	} else {
		Output("SOLID:");
	}
	Output("%X,",brush->color[0]);
	Output("%X,",brush->color[1]);
	Output("%X,",brush->color[2]);
	Output("%X", brush->color[3]);
	if (brush->pbrush) {
		Output(":width=%d",brush->pbrush->width);
		Output(":height=%d",brush->pbrush->height);
		Output(":pitch=%d",brush->pbrush->pitch);
	}
	Output("\n");

	currentGS.sCol = *brush;

	return OPVP_OK;
}

/*
 * SetFillColor
 */
static
int	SetFillColor(
	int		printerContext,
	OPVP_Brush	*brush
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}
	if (!brush) {
		errorno = OPVP_PARAMERROR;
		return -1;
	}

	Output("SetFillColor:");
	if (brush->pbrush) {
		Output("BRUSH:");
	} else {
		Output("SOLID:");
	}
	Output("%X,",brush->color[0]);
	Output("%X,",brush->color[1]);
	Output("%X,",brush->color[2]);
	Output("%X", brush->color[3]);
	if (brush->pbrush) {
		Output(":width=%d",brush->pbrush->width);
		Output(":height=%d",brush->pbrush->height);
		Output(":pitch=%d",brush->pbrush->pitch);
	}
	Output("\n");

	currentGS.fCol = *brush;

	return OPVP_OK;
}

/*
 * SetBgColor
 */
static
int	SetBgColor(
	int		printerContext,
	OPVP_Brush	*brush
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}
	if (!brush) {
		errorno = OPVP_PARAMERROR;
		return -1;
	}

	Output("SetBgColor:");
	if (brush->pbrush) {
		Output("BRUSH:");
	} else {
		Output("SOLID:");
	}
	Output("%X,",brush->color[0]);
	Output("%X,",brush->color[1]);
	Output("%X,",brush->color[2]);
	Output("%X", brush->color[3]);
	if (brush->pbrush) {
		Output(":width=%d",brush->pbrush->width);
		Output(":height=%d",brush->pbrush->height);
		Output(":pitch=%d",brush->pbrush->pitch);
	}
	Output("\n");

	currentGS.bCol = *brush;

	return OPVP_OK;
}

/*
 * ------------------------------------------------------------------------
 * Path Operations
 * ------------------------------------------------------------------------
 */

/*
 * NewPath
 */
static
int	NewPath(
	int		printerContext
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("NewPath\n");

	return OPVP_OK;
}

/*
 * EndPath
 */
static
int	EndPath(
	int		printerContext
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("EndPath\n");

	return OPVP_OK;
}

/*
 * StrokePath
 */
static
int	StrokePath(
	int		printerContext
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("StrokePath\n");

	return OPVP_OK;
}

/*
 * FillPath
 */
static
int	FillPath(
	int		printerContext
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("FillPath\n");

	return OPVP_OK;
}

/*
 * StrokeFillPath
 */
static
int	StrokeFillPath(
	int		printerContext
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("StrokeFillPath\n");

	return OPVP_OK;
}

/*
 * SetClipPath
 */
static
int	SetClipPath(
	int		printerContext,
	OPVP_ClipRule	clipRule
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("ResetClipPath:clipRule=%d\n",clipRule);

	clip = clipRule;

	return OPVP_OK;
}

static
int	ResetClipPath(
	int		printerContext
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("ResetClipPath\n");

	return OPVP_OK;
}

/*
 * SetCurrentPoint
 */
static
int	SetCurrentPoint(
	int		printerContext,
	OPVP_Fix	x,
	OPVP_Fix	y
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("SetCurrentPoint:(x,y)=(%F,",x);
	Output("%F)\n",y);

	currentGS.pos.x = x;
	currentGS.pos.y = y;

	return OPVP_OK;
}

/*
 * LinePath
 */
static
int	LinePath(
	int		printerContext,
	int		flag,
	int		npoints,
	OPVP_Point	*points
	)
{
	errorno = OPVP_OK;
	int		i;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}
	if (!points) {
		errorno = OPVP_PARAMERROR;
		return -1;
	}

	Output("LinePath:flag=%d:",flag);
	Output("(%F,",currentGS.pos.x);
	Output("%F)",currentGS.pos.y);

	for (i=0;i<npoints;i++) {
		Output("-(%F,",(points[i]).x);
		Output("%F)",(points[i]).y);
		currentGS.pos.x = (points[i]).x;
		currentGS.pos.y = (points[i]).y;
	}
	Output("\n");

	return OPVP_OK;
}

/*
 * PolygonPath
 */
static
int	PolygonPath(
	int		printerContext,
	int		npolygons,
	int		*nvertexes,
	OPVP_Point	*points
	)
{
	errorno = OPVP_OK;
	int		i, j, p;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}
	if (!nvertexes) {
		errorno = OPVP_PARAMERROR;
		return -1;
	}
	if (!points) {
		errorno = OPVP_PARAMERROR;
		return -1;
	}

	Output("PolygonPath");

	for (p=0,i=0;i<npolygons;i++) {
		Output(":(%F,",(points[p]).x);
		Output("%F)",(points[p]).y);
		currentGS.pos.x = (points[p]).x;
		currentGS.pos.y = (points[p]).y;
		for (j=1;j<nvertexes[i];j++) {
			p++;
			Output("-(%F,",(points[p]).x);
			Output("%F)",(points[p]).y);
		}
	}
	Output("\n");

	return OPVP_OK;
}

/*
 * RectanglePath
 */
static
int	RectanglePath(
	int		printerContext,
	int		nrectangles,
	OPVP_Rectangle	*rectangles
	)
{
	errorno = OPVP_OK;
	int		i;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}
	if (!rectangles) {
		errorno = OPVP_PARAMERROR;
		return -1;
	}

	Output("RectanglePath");

	for (i=0;i<nrectangles;i++) {
		Output(":(%F,",(rectangles[i]).p0.x);
		Output("%F)-(",(rectangles[i]).p0.y);
		Output("%F,",(rectangles[i]).p1.x);
		Output("%F)",(rectangles[i]).p1.y);
		currentGS.pos.x = (rectangles[i]).p0.x;
		currentGS.pos.y = (rectangles[i]).p0.y;
	}
	Output("\n");

	return OPVP_OK;
}

/*
 * RoundRectanglePath
 */
static
int	RoundRectanglePath(
	int		printerContext,
	int		nrectangles,
	OPVP_RoundRectangle	*rectangles
	)
{
	errorno = OPVP_OK;
	int		i;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}
	if (!rectangles) {
		errorno = OPVP_PARAMERROR;
		return -1;
	}

	Output("RoundRectanglePath");

	for (i=0;i<nrectangles;i++) {
		Output(":(%F,",(rectangles[i]).p0.x);
		Output("%F)-(",(rectangles[i]).p0.y);
		Output("%F,",(rectangles[i]).p1.x);
		Output("%F)",(rectangles[i]).p1.y);
		currentGS.pos.x = (rectangles[i]).p0.x;
		currentGS.pos.y = (rectangles[i]).p0.y;
	}
	Output("\n");

	return OPVP_OK;
}

/*
 * BezierPath
 */
static
int	BezierPath(
	int		printerContext,
	int		npoints,
	OPVP_Point	*points
	)
{
	errorno = OPVP_OK;
	int		i;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}
	if (!points) {
		errorno = OPVP_PARAMERROR;
		return -1;
	}

	Output("BezierPath",NULL);

	for (i=0;i<npoints;i++) {
		Output("-(%F,",(points[i]).x);
		Output("%F)",(points[i]).y);
		currentGS.pos.x = (points[i]).x;
		currentGS.pos.y = (points[i]).y;
	}
	Output("\n");

	return OPVP_OK;
}

/*
 * ArcPath
 */
static
int	ArcPath(
	int		printerContext,
	int		kind,
	int		dir,
	OPVP_Fix	bbx0,
	OPVP_Fix	bby0,
	OPVP_Fix	bbx1,
	OPVP_Fix	bby1,
	OPVP_Fix	x0,
	OPVP_Fix	y0,
	OPVP_Fix	x1,
	OPVP_Fix	y1
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("ArcPath:kind=%d",kind);
	Output(":dir=%d",dir);
	Output(":(%F,",bbx0);
	Output("%F)-(",bby0);
	Output("%F,",bbx1);
	Output("%F)",bby1);
	Output(":(%F,",x0);
	Output("%F)-(",y0);
	Output("%F,",x1);
	Output("%F)\n",y1);

	currentGS.pos.x = (bbx0<bbx1 ? bbx0 : bbx1);
	currentGS.pos.y = (bby0<bby1 ? bby0 : bby1);

	return OPVP_OK;
}

/*
 * ------------------------------------------------------------------------
 * Text Operations (This function is obsolete)
 * ------------------------------------------------------------------------
 */

/*
 * DrawBitmapText (This function is obsolete)
 */
static
int	DrawBitmapText(
	int		printerContext,
	int		width,
	int		height,
	int		pitch,
	void		*fontdata
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}
	if (!fontdata) {
		errorno = OPVP_PARAMERROR;
		return -1;
	}

	Output("DrawBitmapText:width=%d",width);
	Output(":height=%d",height);
	Output(":pitch=%d",pitch);

	return OPVP_OK;
}

/*
 * ------------------------------------------------------------------------
 * Bitmap Image Operations
 * ------------------------------------------------------------------------
 */

/*
 * DrawImage
 */
static
int	DrawImage(
	int		printerContext,
	int		sourceWidth,
	int		sourceHeight,
	int		colorDepth,
	OPVP_ImageFormat	imageFormat,
	OPVP_Rectangle	destinationSize,
	int		count,
	void		*imagedata
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}
	if (!imagedata) {
		errorno = OPVP_PARAMERROR;
		return -1;
	}

	Output("DrawImage:sourceWidth=%d",sourceWidth);
	Output(":sourceHeight=%d",sourceHeight);
	Output(":colorDepth=%d",colorDepth);
	Output(":imageFormat=%d",imageFormat);
	Output(":(%F,",destinationSize.p0.x);
	Output("%F)-(",destinationSize.p0.y);
	Output("%F,",destinationSize.p1.x);
	Output("%F)",destinationSize.p1.y);
	Output(":count=%d\n",count);
	return OPVP_OK;
}

/*
 * StartDrawImage
 */
static
int	StartDrawImage(
	int		printerContext,
	int		sourceWidth,
	int		sourceHeight,
	int		colorDepth,
	OPVP_ImageFormat	imageFormat,
	OPVP_Rectangle	destinationSize
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("StartDrawImage:sourceWidth=%d",sourceWidth);
	Output(":sourceHeight=%d",sourceHeight);
	Output(":colorSpace=%d",colorDepth);
	Output(":imageFormat=%d",imageFormat);
	Output(":(%F,",destinationSize.p0.x);
	Output("%F)-(",destinationSize.p0.y);
	Output("%F,",destinationSize.p1.x);
	Output("%F)\n",destinationSize.p1.y);

	return OPVP_OK;
}

/*
 * TransferDrawImage
 */
static
int	TransferDrawImage(
	int		printerContext,
	int		count,
	void		*imagedata
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}
	if (!imagedata) {
		errorno = OPVP_PARAMERROR;
		return -1;
	}

	Output("TransferDrawImage:count=%d\n",count);

	return OPVP_OK;
}

/*
 * EndDrawImage
 */
static
int	EndDrawImage(
	int		printerContext
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("EndDrawImage\n",printerContext);

	return OPVP_OK;
}

/*
 * ------------------------------------------------------------------------
 * Scan Line Operations
 * ------------------------------------------------------------------------
 */

/*
 * StartScanline
 */
static
int	StartScanline(
	int		printerContext,
	int		yposition
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("StartScanline:yposition=%d\n",yposition);

	return OPVP_OK;
}

/*
 * Scanline
 */
static
int	Scanline(
	int		printerContext,
	int		nscanpairs,
	int		*scanpairs
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}
	if (!scanpairs) {
		errorno = OPVP_PARAMERROR;
		return -1;
	}

	Output("Scanline:nscanpairs=%d\n",nscanpairs);

	return OPVP_OK;
}

/*
 * EndScanline
 */
static
int	EndScanline(
	int		printerContext
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("EndScanline\n");

	return OPVP_OK;
}

/*
 * ------------------------------------------------------------------------
 * Raster Image Operations
 * ------------------------------------------------------------------------
 */

/*
 * StartRaster
 */
static
int	StartRaster(
	int		printerContext,
	int		rasterWidth
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("StartRaster:rasterWidth=%d\n",rasterWidth);

	return OPVP_OK;
}

/*
 * TransferRasterData
 */
static
int	TransferRasterData(
	int		printerContext,
	int		count,
	unsigned char	*data
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}
	if (!data) {
		errorno = OPVP_PARAMERROR;
		return -1;
	}

	Output("TransferRaster:count=%d\n",count);

	return OPVP_OK;
}

/*
 * SkipRaster
 */
static
int	SkipRaster(
	int		printerContext,
	int		count
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("SkipRaster:count=%d\n",count);

	return OPVP_OK;
}

/*
 * EndRaster
 */
static
int	EndRaster(
	int		printerContext
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("EndRaster\n");

	return OPVP_OK;
}

/*
 * ------------------------------------------------------------------------
 * Raster Image Operations
 * ------------------------------------------------------------------------
 */

/*
 * StartStream
 */
static
int	StartStream(
	int		printerContext
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("StartStream\n");

	return OPVP_OK;
}

/*
 * TransferStreamData
 */
static
int	TransferStreamData(
	int		printerContext,
	int		count,
	void		*data
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}
	if (!data) {
		errorno = OPVP_PARAMERROR;
		return -1;
	}

	Output("TransferStreamData:count=%d\n",count);

	return OPVP_OK;
}

/*
 * EndStream
 */
static
int	EndStream(
	int		printerContext
	)
{
	errorno = OPVP_OK;

	if (pContext != printerContext) {
		errorno = OPVP_BADCONTEXT;
		return -1;
	}

	Output("EndStream\n");

	return OPVP_OK;
}

/*
 * private functions
 */
static
void	SetApiList(
	void
	)
{
	apiList.OpenPrinter	= OpenPrinter;
	apiList.ClosePrinter	= ClosePrinter;
	apiList.StartJob	= StartJob;
	apiList.EndJob		= EndJob;
	apiList.StartDoc	= StartDoc;
	apiList.EndDoc		= EndDoc;
	apiList.StartPage	= StartPage;
	apiList.EndPage		= EndPage;
	apiList.ResetCTM	= ResetCTM;
	apiList.SetCTM		= SetCTM;
	apiList.GetCTM		= GetCTM;
	apiList.InitGS		= InitGS;
	apiList.SaveGS		= SaveGS;
	apiList.RestoreGS	= RestoreGS;
	apiList.QueryColorSpace	= QueryColorSpace;
	apiList.SetColorSpace	= SetColorSpace;
	apiList.GetColorSpace	= GetColorSpace;
	apiList.QueryROP	= QueryROP;
	apiList.SetROP		= SetROP;
	apiList.GetROP		= GetROP;
	apiList.SetFillMode	= SetFillMode;
	apiList.GetFillMode	= GetFillMode;
	apiList.SetAlphaConstant= SetAlphaConstant;
	apiList.GetAlphaConstant= GetAlphaConstant;
	apiList.SetLineWidth	= SetLineWidth;
	apiList.GetLineWidth	= GetLineWidth;
	apiList.SetLineDash	= SetLineDash;
	apiList.GetLineDash	= GetLineDash;
	apiList.SetLineDashOffset= SetLineDashOffset;
	apiList.GetLineDashOffset= GetLineDashOffset;
	apiList.SetLineStyle	= SetLineStyle;
	apiList.GetLineStyle	= GetLineStyle;
	apiList.SetLineCap	= SetLineCap;
	apiList.GetLineCap	= GetLineCap;
	apiList.SetLineJoin	= SetLineJoin;
	apiList.GetLineJoin	= GetLineJoin;
	apiList.SetMiterLimit	= SetMiterLimit;
	apiList.GetMiterLimit	= GetMiterLimit;
	apiList.SetPaintMode	= SetPaintMode;
	apiList.GetPaintMode	= GetPaintMode;
	apiList.SetStrokeColor	= SetStrokeColor;
	apiList.SetFillColor	= SetFillColor;
	apiList.SetBgColor	= SetBgColor;
	apiList.NewPath		= NewPath;
	apiList.EndPath		= EndPath;
	apiList.StrokePath	= StrokePath;
	apiList.FillPath	= FillPath;
	apiList.StrokeFillPath	= StrokeFillPath;
	apiList.SetClipPath	= SetClipPath;
	apiList.SetCurrentPoint	= SetCurrentPoint;
	apiList.LinePath	= LinePath;
	apiList.PolygonPath	= PolygonPath;
	apiList.RectanglePath	= RectanglePath;
	apiList.RoundRectanglePath= RoundRectanglePath;
	apiList.BezierPath	= BezierPath;
	apiList.ArcPath		= ArcPath;
	apiList.DrawBitmapText	= DrawBitmapText;
	apiList.DrawImage	= DrawImage;
	apiList.StartDrawImage	= StartDrawImage;
	apiList.TransferDrawImage= TransferDrawImage;
	apiList.EndDrawImage	= EndDrawImage;
	apiList.StartScanline	= StartScanline;
	apiList.Scanline	= Scanline;
	apiList.EndScanline	= EndScanline;
	apiList.StartRaster	= StartRaster;
	apiList.TransferRasterData= TransferRasterData;
	apiList.SkipRaster	= SkipRaster;
	apiList.EndRaster	= EndRaster;
	apiList.StartStream	= StartStream;
	apiList.TransferStreamData= TransferStreamData;
	apiList.EndStream	= EndStream;
	apiList.QueryDeviceCapability = QueryDeviceCapability;
	apiList.QueryDeviceInfo = QueryDeviceInfo;
	apiList.ResetClipPath = ResetClipPath;
}

