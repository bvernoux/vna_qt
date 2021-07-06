//***************************************************************************
//
// For each destination X, find pair of source points containing it and interpolate the corresponding
// source Y interval to destination Y
//
//***************************************************************************

void lerp_gen (DOUBLE *src_X,  DOUBLE *src_Y,  S32 src_len, //)
               DOUBLE *dest_X, DOUBLE *dest_Y, S32 dest_len)
{
   if ((dest_X[0] < src_X[0]) || (dest_X[dest_len-1] > src_X[src_len-1]))
      {
      assert(0);
      }

   S32 s=0,d=0;

   for (d=0; d < dest_len; d++)
      {
      DOUBLE x = dest_X[d];

      for (s=0; s < src_len-1; s++)
         {
         if ((src_X[s] <= x) && (src_X[s+1] >= x))
            {
            break;
            }
         }

      assert(s < src_len-1);

      DOUBLE ds = src_X[s+1] - src_X[s];

      assert(fabs(ds) > 1E-30);

      DOUBLE alpha = (x - src_X[s]) / ds;       // fraction from s to s+1

      ds = src_Y[s+1] - src_Y[s];

      dest_Y[d] = src_Y[s] + (ds * alpha);
      }
}

//***************************************************************************
//
// Cubic spline interpolators from Wolberg, Digital Image Warping, p. 293
//
// Alternative version (spline_gen) derived from Numerical Recipes 3rd Edition, p. 121
// has slightly better endpoint behavior
//
//***************************************************************************

void spline_gen (DOUBLE *src_X,  DOUBLE *src_Y,  S32 src_len, //)
                 DOUBLE *dest_X, DOUBLE *dest_Y, S32 dest_len)
{
   if ((dest_X[0] < src_X[0]) || (dest_X[dest_len-1] > src_X[src_len-1]))
      {
      assert(0);
      }

   //
   // Calculate second derivatives at input points, guarding against
   // division by infinitesimals or zero that can happen with
   // vertical or coincident segments
   //

   DOUBLE *D2  = (DOUBLE *) malloc(src_len * sizeof(DOUBLE)); if (D2 == NULL) {           return; }
   DOUBLE *YD  = (DOUBLE *) malloc(src_len * sizeof(DOUBLE)); if (YD == NULL) { free(D2); return; }

   D2[0] = YD[0] = 0.0;

   for (S32 i=1; i < src_len-1; i++)
      {
      DOUBLE epsilon = fabs(src_X[i]) * 1E-6;

      DOUBLE h0 =  src_X[i]   - src_X[i-1];
      DOUBLE h1 =  src_X[i+1] - src_X[i-1];
      DOUBLE h2 =  src_X[i+1] - src_X[i];

      if (fabs(h0) < epsilon) h0 = epsilon;
      if (fabs(h1) < epsilon) h1 = epsilon;
      if (fabs(h2) < epsilon) h2 = epsilon;

      DOUBLE r0 = (src_Y[i]   - src_Y[i-1]) / h0;
      DOUBLE r1 = (src_Y[i+1] - src_Y[i])   / h2;

      DOUBLE h = h0 / h1;
      DOUBLE p = 1.0 / (h * D2[i-1] + 2.0);

      D2[i] = (h - 1.0) * p;
      YD[i] = (((6.0 * (r1 - r0)) / h1) - (h * YD[i-1])) * p;
      }

   D2[src_len-1] = 0.0;

   for (S32 i=src_len-2; i >= 0; i--)
      {
      D2[i] = (D2[i] * D2[i+1]) + YD[i];
      }

   //
   // For each output X...
   //

   S32 cur = 0;

   for (S32 i=0; i < dest_len; i++)
      {
      DOUBLE x = dest_X[i];

      //
      // Find input interval containing this X
      //

      while ((src_X[cur+1] <= x) && (cur+1 < src_len))
         {
         cur++;
         }

      //
      // Perform cubic spline interpolation
      //

      S32 next = cur+1;

      DOUBLE h = src_X[next] - src_X[cur];

      if (h <= 0.0) h = 0.0001;                 // HACK
      assert(h > 0.0);                          // TODO: fired in Phase view 

      DOUBLE a = (src_X[next] - x) / h;
      DOUBLE b = (x - src_X[cur])  / h;

      dest_Y[i] = (a*src_Y[cur]) + (b*src_Y[next]) + (((a*a*a-a)*D2[cur]) + ((b*b*b-b)*D2[next])) * (h*h) / 6.0;
      }

   free(YD);
   free(D2);
}

static void tridiag_gen(DOUBLE *A, DOUBLE *B, DOUBLE *C, DOUBLE *D, S32 len)
{
   S32 i;
   DOUBLE b, *F;

   F = (DOUBLE *) malloc(len * sizeof(DOUBLE)); 
   if (F == NULL) return;

   b = B[0];
   D[0] = D[0] / b;

   for (i=1; i < len; i++)
      {
      F[i] = C[i-1] / b;
      b = B[i] - A[i] * F[i];

      assert(b != 0.0);

      D[i] = (D[i] - D[i-1] * A[i]) / b;
      }

   for (i=len-2; i >= 0; i--)
      {
      D[i] -= (D[i+1] * F[i+1]);
      }

   free(F);
}

static void getYD_gen(DOUBLE *X, DOUBLE *Y, DOUBLE *YD, S32 len)
{
   S32 i;
   DOUBLE h0, h1, r0, r1, *A, *B, *C;

   A = (DOUBLE *) malloc(len * sizeof(DOUBLE)); if (A == NULL) {                   return; }
   B = (DOUBLE *) malloc(len * sizeof(DOUBLE)); if (B == NULL) { free(A);          return; }
   C = (DOUBLE *) malloc(len * sizeof(DOUBLE)); if (C == NULL) { free(B); free(A); return; }

   h0 = X[1] - X[0];
   h1 = X[2] - X[1];
   r0 = (Y[1] - Y[0]) / h0;
   r1 = (Y[2] - Y[1]) / h1;

   B[0] = h1 * (h0 + h1);
   C[0] = (h0 + h1) * (h0 + h1);
   YD[0] = r0 * (3.0 * h0 * h1 + 2.0 * h1 * h1) + r1 * h0 * h0;

   for (i=1; i < len-1; i++)
      {
      h0 = X[i] - X[i-1];
      h1 = X[i+1] - X[i];
      r0 = (Y[i] - Y[i-1]) / h0;
      r1 = (Y[i+1] - Y[i]) / h1;
      A[i] = h1;
      B[i] = 2 * (h0 + h1);
      C[i] = h0;
      YD[i] = 3.0 * (r0 * h1 + r1 * h0);
      }

   A[i] = (h0 + h1) * (h0 + h1);
   B[i] = h0 * (h0 + h1);
   YD[i] = r0 * h1 * h1 + r1 * (3.0 * h0 * h1 + 2.0 * h0 * h0);

   tridiag_gen(A, B, C, YD, len);

   free(C);
   free(B);
   free(A);
}

void ispline_gen(DOUBLE *X1, DOUBLE *Y1, S32 len1, //)
                 DOUBLE *X2, DOUBLE *Y2, S32 len2)
{
   S32 i,j;
   DOUBLE *YD=NULL, A0=0.0, A1=0.0, A2=0.0, A3=0.0, x=0.0, dx=0.0, dy=0.0, p1=0.0, p2=0.0, p3=0.0;

   YD = (DOUBLE *) malloc(len1 * sizeof(DOUBLE));
   if (YD == NULL) return;

   getYD_gen(X1, Y1, YD, len1);

   if (X2[0] < X1[0] || X2[len2-1] > X1[len1-1])
      {
      assert(0);
      }

   p3 = X2[0] - 1;
   for (i=j=0; i < len2; i++)
      {
      p2 = X2[i];

      if (p2 > p3)
         {
         for (; j < len1 && p2 > X1[j]; j++);

         if (p2 < X1[j]) j--;

         p1 = X1[j];
         p3 = X1[j+1];

         dx = 1.0 / (X1[j+1] - X1[j]);
         dy = (Y1[j+1] - Y1[j]) * dx;

         A0 = Y1[j];
         A1 = YD[j];
         A2 = dx * (3.0 * dy - 2.0 * YD[j] - YD[j+1]);
         A3 = dx * dx * (-2.0 * dy + YD[j] + YD[j+1]);
         }

      x = p2 - p1;
      Y2[i] = ((A3 * x + A2) * x + A1) * x + A0;
      }

   free(YD);
}

static void tridiag(DOUBLE *D, S32 len)
{
   S32 i;
   DOUBLE *C;

   C = (DOUBLE *) malloc(len * sizeof(DOUBLE));
   if (C == NULL) return;

   D[0] = 0.5F * D[0];
   D[1] = (D[1] - D[0]) / 2.0;
   C[1] = 2.0;

   for (i=2; i < len-1; i++)
      {
      C[i] = 1.0 / (4.0 - C[i-1]);
      D[i] = (D[i] - D[i-1]) / (4.0 - C[i]);
      }

   C[i] = 1.0 / (4.0 - C[i-1]);
   D[i] = (D[i] - 4.0*D[i-1]) / (2.0 - 4.0*C[i]);

   for (i=len-2; i >= 0; i--)
      {
      D[i] -= (D[i+1] * C[i+1]);
      }

   free(C);
}

static void getYD(DOUBLE *Y, DOUBLE *YD, S32 len)
{
   S32 i;

   YD[0] = -5.0*Y[0] + 4.0*Y[1] + Y[2];

   for (i=1; i < len-1; i++)
      {
      YD[i] = 3.0 * (Y[i+1] - Y[i-1]);
      }

   YD[len-1] = -Y[len-3] - 4.0*Y[len-2] + 5.0*Y[len-1];

   tridiag(YD,len);
}

void ispline(DOUBLE *Y1, S32 len1, //)
             DOUBLE *Y2, S32 len2)
{
   S32 i,oip;
   DOUBLE *YD=NULL, A0=0.0, A1=0.0, A2=0.0, A3=0.0, x=0.0, p=0.0, fctr=0.0;

   YD = (DOUBLE *) malloc(len1 * sizeof(DOUBLE));
   if (YD == NULL) return;

   getYD(Y1,YD,len1);

   oip = -1;
   fctr = DOUBLE(len1-2) / (DOUBLE) len2; // Original: len1-1 / len2-1, which made ip+1 overflow in A2,A3 calculations

   for (i=0,p=0.5F; i < len2; i++)        // Original: p=0.0, which is bad for symmetry
      {
      S32 ip = (S32) p;

      if (ip != oip)
         {
         oip = ip;

         A0 = Y1[ip];
         A1 = YD[ip];
         A2 = 3.0 * (Y1[ip+1] - Y1[ip]) - 2.0 * YD[ip] - YD[ip+1];
         A3 = -2.0 * (Y1[ip+1] - Y1[ip]) + YD[ip] + YD[ip+1];
         }

      x = p - ip;
      Y2[i] = ((A3 * x + A2) * x + A1) * x + A0;

      p += fctr;
      }

   free(YD);
}

//
// Alternative version used by TCheck utility
//

void ispline_t(DOUBLE *Y1, S32 len1, //)
               DOUBLE *Y2, S32 len2)
{
   S32 i,oip;
   DOUBLE *YD, A0, A1, A2, A3, x, p, fctr;

   YD = (DOUBLE *) malloc(len1 * sizeof(DOUBLE));
   if (YD == NULL) return;

   getYD(Y1,YD,len1);

   oip = -1;
   fctr = DOUBLE(len1-1) / (DOUBLE) (len2-1);

   A0 = A1 = A2 = A3 = 0;

   for (i=0,p=0.0F; i < len2; i++)      
      {
      S32 ip = (S32) p;

      if (ip != oip)
         {
         oip = ip;

         A0 = Y1[ip];
         A1 = YD[ip];
         A2 = 3.0 * (Y1[ip+1] - Y1[ip]) - 2.0 * YD[ip] - YD[ip+1];
         A3 = -2.0 * (Y1[ip+1] - Y1[ip]) + YD[ip] + YD[ip+1];
         }

      x = p - ip;
      Y2[i] = ((A3 * x + A2) * x + A1) * x + A0;

      p += fctr;
      }

   free(YD);
}


