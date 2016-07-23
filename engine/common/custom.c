/*
custom.c - custom resources
Copyright (C) 2016 a1batross

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "common.h"
#include "server.h"

typedef struct
{
	char	*filename;
	int	num_items;
	file_t	*file;	// pointer to wadfile
} cachewad_t;

int CustomDecal_Init( cachewad_t *wad, byte *data, int size, int playernum )
{
	// TODO: implement
	return 0;
}

void *CustomDecal_Validate( byte *data, int size )
{
	// TODO: implement
	return NULL;
}

int COM_CreateCustomization( customization_t *pListHead, resource_t *pResource, int playernumber, int flags, customization_t **pCust, int *nLumps )
{
	customization_t	*pRes;
	cachewad_t	*pldecal;
	qboolean		found_problem;

	found_problem = false;

	ASSERT( pResource != NULL );

	if( pCust )
		*pCust = NULL;

	pRes = Z_Malloc( sizeof( customization_t ));
	Q_memset( pRes, 0, sizeof( customization_t ));

	pRes->resource = *pResource;

	if( pResource->nDownloadSize <= 0 )
	{
		found_problem = true;
	}
	else
	{
		pRes->bInUse = true;

		if(( flags & FCUST_FROMHPAK ) && !HPAK_GetDataPointer( "custom.hpk", pResource, (byte **)&(pRes->pBuffer), NULL ))
		{
			found_problem = true;
		}
		else
		{
			pRes->pBuffer = FS_LoadFile( pResource->szFileName, NULL, false );

			if( ( pRes->resource.ucFlags & RES_CUSTOM ) && pRes->resource.type == t_decal )
			{
				pRes->resource.playernum = playernumber;

				if( !CustomDecal_Validate( pRes->pBuffer, pResource->nDownloadSize ))
				{
					found_problem = true;
				}
				else if( flags & RES_CUSTOM )
				{
					// do nothing
				}
				else
				{
					pRes->pInfo = Z_Malloc( sizeof( cachewad_t ));
					pldecal = pRes->pInfo;

					if( pResource->nDownloadSize < 1024 || pResource->nDownloadSize > 20480 )
					{
						found_problem = true;
					}
					else if( !CustomDecal_Init( pldecal, pRes->pBuffer, pResource->nDownloadSize, playernumber ))
					{
						found_problem = true;
					}
					else if( pldecal->num_items <= 0 )
					{
						found_problem = true;
					}
					else
					{
						if( nLumps ) *nLumps = pldecal->num_items;

						pRes->bTranslated = true;
						pRes->nUserData1  = 0;
						pRes->nUserData2  = pldecal->num_items;

						if( flags & FCUST_WIPEDATA )
						{
							Mem_Free( pldecal->filename );
							FS_Close( pldecal->file );
							Mem_Free( pldecal );
							pRes->pInfo = NULL;
						}
					}
				}
			}
		}
	}

	if( !found_problem )
	{
		if( pCust ) *pCust = pRes;

		pRes->pNext = pListHead->pNext;
		pListHead->pNext = pRes;
	}
	else
	{
		if( pRes->pBuffer ) Mem_Free( pRes->pBuffer );
		if( pRes->pInfo )   Mem_Free( pRes->pInfo );
		Mem_Free( pRes );
	}

	return !found_problem;
}

void COM_ClearCustomizationList( customization_t *pHead, qboolean bCleanDecals )
{
	customization_t	*pNext, *pCur;
	cachewad_t	*wad;

	if( !pHead || !pHead->pNext )
		return;

	pCur = pHead->pNext;

	for( pCur = pHead->pNext; pCur; pCur = pNext )
	{
		pNext = pCur->pNext;

		if( pCur->bInUse )
		{
			if( pCur->pBuffer )
				Mem_Free( pCur->pBuffer );

			if( pCur->pInfo )
			{
				if( pCur->resource.type == t_decal )
				{
					wad = (cachewad_t *)pCur->pInfo;
					Mem_Free( wad->filename );
					FS_Close( wad->file );
				}
				Mem_Free( pCur->pInfo );
			}
		}
		Mem_Free( pCur );
	}

	pCur->pNext = NULL;
}

int COM_SizeofResourceList( resource_t *pList, resourceinfo_t *ri )
{
	resource_t *p;
	int nSize;

	Q_memset( ri, 0, sizeof( resourceinfo_t ) );

	for( nSize = 0, p = pList->pNext; p != pList; p = p->pNext )
	{
		// invalid type
		if( p->type >= 8 )
			continue;

		nSize += p->nDownloadSize;
		if (p->type != t_model || p->nIndex != 1)
		{
			if ((unsigned int)p->type < sizeof(ri->info))
				ri->info[p->type].size += p->nDownloadSize;
		}
		else
		{
			ri->info[t_world].size += p->nDownloadSize;
		}
	}

	return nSize;
}
