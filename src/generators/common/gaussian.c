/*
 * Copyright(C) 2016 Pedro H. Penna <pedrohenriquepenna@gmail.com>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <assert.h>
#include <stdlib.h>

#include <common.h>

/**
 * @brief Builds a Gaussian sample.
 * 
 * @param nsamples   Number of samples.
 * @param nintervals Number of sampling intervals.
 * 
 * @returns A Gaussian sample.
 */
double *gaussian(int nsamples, int nintervals)
{
	int k;
	int residual;
	int *histogram;
	double *x;
	
	/* Sanity check. */
	assert(nsamples > 0);
	assert(nintervals > 0);

	histogram = smalloc(nintervals*sizeof(int));
	x = smalloc(nsamples*sizeof(double));

	residual = 0;
	for (int i = 0; i < nintervals/2; i++)
	{
		int freq = nsamples/(1 << (i + 2));
		
		residual += freq;
		histogram[nintervals/2 - i - 1] = freq;
		histogram[nintervals/2 + i] = freq;
	}
	residual = nsamples - (residual*2);
	
	if (residual > 0)
	{
		histogram[0] += residual/2;
		histogram[nintervals - 1] += residual/2;
	}
	
	
	/* Generate input data. */
	k = 0;
	for (int i = 0; i < nintervals; i++)
	{
		for (int j = 0; j < histogram[i]; j++)
			x[k++] = i;
	}
	
	/* House keeping. */
	free(histogram);
	
	return (x);
}
