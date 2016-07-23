/*
sv_upload.c - downloading custom resources
Copyright (C) 2010 Uncle Mike
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

byte HEX_CharToInt( char c )
{
	if( c >= '0' && c <= '9' )
		return (c - '0');

	if( c >= 'A' && c <= 'F' )
		return (c - 'A' + 10);

	if( c >= 'a' && c <= 'f' )
		return (c - 'a' + 10);
}

void HEX_Convert( const char *in, int len, byte *out )
{
	byte *p;
	int i;

	for( p = out, i = 0; i < len; i += 2, p++ )
	{
		if( !in[i] || in[i + 1] )
			break;


		*p = HEX_CharToInt(in[i]) << 4 | HEX_CharToInt(in[i + 1]);
	}
}

qboolean SV_CheckFile( sizebuf_t *msg, char *filename )
{
	resource_t p = { 0 };

	if( !sv_allow_upload->value )
		return true;

	if( Q_strlen( filename ) == 36 && !Q_strnicmp( filename, "!MD5", 4 ) )
	{
		HEX_Convert(filename + 4, 32, p.rgucMD5_hash );
		if( HPAK_GetDataPointer("custom.hpk", &p, 0, 0) )
			return true;
	}

	BF_WriteByte( msg, svc_stufftext );
	BF_WriteString( msg, va("upload \"!MD5%s\"\n", MD5_Print( p.rgucMD5_hash ) ) );
}

void SV_AddToResourceList( resource_t *res, resource_t *list )
{
	// resource already linked
	ASSERT( res->pPrev || res->pNext );

	res->pPrev = list->pPrev;
	res->pNext = list;

	list->pPrev->pNext = res;
	list->pPrev = res;
}

void SV_RemoveFromResourceList(resource_t *res)
{
	res->pPrev->pNext = res->pNext;
	res->pNext->pPrev = res->pPrev;
	res->pPrev = NULL;
	res->pNext = NULL;
}

void SV_ClearResourceList( resource_t *list )
{
	resource_t *p, *n;

	for( p = list->pNext; p || p == list; p = n )
	{
		n = p->pNext;

		SV_RemoveFromResourceList( p );
		Mem_Free( p );
	}

	list->pPrev = list;
	list->pNext = list;
}

void SV_ClearResourceLists( sv_client_t *cl )
{
	ASSERT( cl );

	SV_ClearResourceList( &cl->resourcesneeded );
	SV_ClearResourceList( &cl->resourcesonhand );
}

void SV_CreateCustomizationList( sv_client_t *cl )
{
	resource_t	*pRes;
	customization_t	*pCust, *pNewCust;
	int		duplicated, lump_count;

	COM_ClearCustomizationList( &cl->customization, false );

	for( pRes = cl->resourcesonhand.pNext; pRes != &cl->resourcesonhand; pRes = pRes->pNext )
	{
		duplicated = false;

		for( pCust = cl->customization.pNext; pCust != NULL; pCust = pCust->pNext )
		{
			if( !Q_memcmp( pCust->resource.rgucMD5_hash, pRes->rgucMD5_hash, 16 ))
			{
				duplicated = true;
				break;
			}
		}

		if( duplicated )
		{
			MsgDev( D_WARN, "CreateCustomizationList: duplicate resource detected.\n" );
			continue;
		}

		// create it.
		lump_count = 0;

		if( !COM_CreateCustomization( &cl->customization, pRes, -1, 3, &pNewCust, &lump_count ))
		{
			if( sv_allow_upload->value )
				MsgDev( D_WARN, "CreateCustomizationList: ignoring invalid custom decal from %s.\n", cl->name );
			else
				MsgDev( D_WARN, "CreateCustomizationList: ignoring custom decal from %s.\n", cl->name );
			continue;
		}

		pNewCust->nUserData2 = lump_count;
		svgame.dllFuncs.pfnPlayerCustomization( cl->edict, pNewCust );
	}
}

void SV_Customization( sv_client_t *cl, resource_t *res, qboolean skip )
{
	int i;
	sv_client_t *host;

	for( i = 0, host = svs.clients; i < sv_maxclients->integer && host != cl; i++, host++ );

	if( i == sv_maxclients->integer )
		MsgDev( D_INFO, "SV_Customization: Couldn't get player index for customization.\n");

	// Send resource to all other active players
	for (i = 0, host = svs.clients; i < sv_maxclients->integer; i++, host++)
	{
		if( host->state < cs_spawned || host->fakeclient )
			continue;

		if( skip && host == cl )
			continue;

		BF_WriteByte( &host->netchan.message, svc_customization );
		BF_WriteByte( &host->netchan.message, svs.spawncount );
		BF_WriteByte( &host->netchan.message, res->type );
		BF_WriteString( &host->netchan.message, res->szFileName );
		BF_WriteShort( &host->netchan.message, res->nIndex );
		BF_WriteLong( &host->netchan.message, res->nDownloadSize );
		BF_WriteByte( &host->netchan.message, res->ucFlags );
		if (res->ucFlags & RES_CUSTOM)
		{
			BF_WriteBytes( &host->netchan.message, res->rgucMD5_hash, 16 );
		}
	}
}

void SV_RegisterResources( sv_client_t *cl )
{
	resource_t *res;

	cl->uploading = false;

	SV_CreateCustomizationList( cl );

	for( res = cl->resourcesonhand.pNext; res != &cl->resourcesonhand; res = res->pNext )
		SV_Customization( cl, res, TRUE);
}

void SV_MoveToOnHandList( sv_client_t *cl, resource_t *res )
{
	ASSERT( res );

	SV_RemoveFromResourceList( res );
	SV_AddToResourceList( res, &cl->resourcesonhand );
}

int SV_EstimateNeededResources( sv_client_t *cl )
{
	resource_t *p;
	int missing;
	int size = 0;

	for( p = cl->resourcesneeded.pNext; p != &cl->resourcesneeded; p = p->pNext )
	{
		if( p->nDownloadSize <= 0 )
			continue;

		if( !(p->ucFlags & RES_CUSTOM) )
			continue;

		if( p->type != t_decal )
			continue;

		missing = HPAK_ResourceForHash("custom.hpk", p->rgucMD5_hash, NULL );
		if( missing )
		{
			size += p->nDownloadSize;
			p->ucFlags |= RES_WASMISSING;
		}
	}

	return size;
}

qboolean SV_UploadComplete( sv_client_t *cl )
{
	if( cl->resourcesneeded.pNext == &cl->resourcesneeded )
	{
		SV_RegisterResources( cl );
		SV_PropogateCustomizations( cl );

		if( sv_allow_upload->value )
			MsgDev( D_INFO, "SV_UploadComplete: Custom resource propogation complete.\n");

		cl->uploaddoneregistering = true;
		return true;
	}

	return false;
}

void SV_RequestMissingResourcesFromClients( void )
{
	int i;
	sv_client_t *cl;
	for( i = 0, cl = svs.clients; i < sv_maxclients->integer; i++, cl++ )
	{
		if( cl->state < cs_spawned )
			continue;

		if( cl->uploading && !cl->uploaddoneregistering )
			SV_UploadComplete( cl );
	}
}

void SV_BatchUploadRequest( sv_client_t *cl )
{
	resource_t *p, *n;
	char filename[PATH_MAX];

	for( p = cl->resourcesneeded.pNext; p != &cl->resourcesneeded; p = n )
	{
		n = p->pNext;

		if( !(p->ucFlags & RES_WASMISSING) )
			SV_MoveToOnHandList( cl, p );
		else if( p->type == t_decal )
		{
			if( p->ucFlags & RES_CUSTOM )
			{
				Q_snprintf( filename, sizeof( filename ), "!MD5%s", MD5_Print( p->rgucMD5_hash ) );
				if( SV_CheckFile( &cl->netchan.message, filename ) )
					SV_MoveToOnHandList( cl, p );
			}
			else
			{
				MsgDev( D_INFO, "Non customization in upload queue!\n" );
				SV_MoveToOnHandList( cl, p );
			}
		}
	}
}

void SV_ParseResourceList( sizebuf_t *msg, sv_client_t *cl )
{
	int i, total, totalsize, bytestodownload;
	resource_t *res;
	resourceinfo_t ri;

	total = BF_ReadShort( msg );

	SV_ClearResourceLists( cl );

	for( i = 0; i < total; i++ )
	{
		res = (resource_t *)Z_Malloc( sizeof( resource_t ) );
		Q_strncpy( res->szFileName, BF_ReadString( msg ), sizeof( res->szFileName ) - 1 );
		res->szFileName[sizeof(res->szFileName) - 1] = 0;
		res->type = BF_ReadByte( msg );
		res->nIndex = BF_ReadShort( msg );
		res->nDownloadSize = BF_ReadLong( msg );
		res->ucFlags = BF_ReadByte( msg ) & (~RES_WASMISSING);
		if( res->ucFlags & RES_CUSTOM )
			BF_ReadBytes( msg, res->rgucMD5_hash, 16 );
		res->pNext = NULL;
		res->pPrev = NULL;

		SV_AddToResourceList( res, &cl->resourcesneeded );

		if( msg->bOverflow || res->type > t_world || res->nDownloadSize <= 0 || res->nDownloadSize > 1024 * 1024 * 1024 )
		{
			SV_ClearResourceLists( cl );
			return;
		}
	}

	if( sv_allow_upload->value )
	{
		MsgDev( D_INFO, "Verifying and uploading resources...\n" );
		totalsize = COM_SizeofResourceList( &cl->resourcesneeded, &ri );

		if( totalsize > 0 )
		{
			MsgDev( D_INFO, "Custom resources total %.2fK", total / 1024.0f );

			if( ri.info[t_model].size )
			{
				total -= ri.info[t_model].size;
				MsgDev( D_INFO, "\tModels: %.2fK\n", total / 1024.0f );
			}
			if( ri.info[t_sound].size )
			{
				total -= ri.info[t_sound].size;
				MsgDev( D_INFO, "\tSounds: %.2fK\n", total / 1024.0f );
			}
			if( ri.info[t_decal].size )
			{
				total -= ri.info[t_decal].size;
				MsgDev( D_INFO, "\tDecals: %.2fK\n", total / 1024.0f );
			}
			if( ri.info[t_skin].size )
			{
				total -= ri.info[t_skin].size;
				MsgDev( D_INFO, "\tSkins: %.2fK\n", total / 1024.0f );
			}
			if( ri.info[t_generic].size )
			{
				total -= ri.info[t_generic].size;
				MsgDev( D_INFO, "\tGeneric: %.2fK\n", total / 1024.0f );
			}
			if( ri.info[t_eventscript].size )
			{
				total -= ri.info[t_eventscript].size;
				MsgDev( D_INFO, "\tEvent Scripts: %.2fK\n", total / 1024.0f );
			}
			MsgDev( D_INFO, "--------------------\n");

			bytestodownload = SV_EstimateNeededResources( cl );

			if( bytestodownload > sv_max_upload->integer * 1024 * 1024 )
			{
				SV_ClearResourceLists( cl );
				return;
			}

			if( bytestodownload > 1024 )
				MsgDev( D_INFO, "Resources to request: %.2fK\n", bytestodownload / 1024.0f );
			else
				MsgDev( D_INFO, "Resources to request: %i bytes", bytestodownload );
		}
	}

	cl->uploading = true;
	cl->uploaddoneregistering = true;

	SV_BatchUploadRequest( cl );
}
