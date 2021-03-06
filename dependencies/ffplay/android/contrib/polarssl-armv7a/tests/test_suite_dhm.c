#include "fct.h"
#include <polarssl/config.h>

#include <polarssl/dhm.h>

#ifdef _MSC_VER
#include <basetsd.h>
typedef UINT32 uint32_t;
#else
#include <inttypes.h>
#endif

/*
 * 32-bit integer manipulation macros (big endian)
 */
#ifndef GET_UINT32_BE
#define GET_UINT32_BE(n,b,i)                            \
{                                                       \
    (n) = ( (uint32_t) (b)[(i)    ] << 24 )             \
        | ( (uint32_t) (b)[(i) + 1] << 16 )             \
        | ( (uint32_t) (b)[(i) + 2] <<  8 )             \
        | ( (uint32_t) (b)[(i) + 3]       );            \
}
#endif

#ifndef PUT_UINT32_BE
#define PUT_UINT32_BE(n,b,i)                            \
{                                                       \
    (b)[(i)    ] = (unsigned char) ( (n) >> 24 );       \
    (b)[(i) + 1] = (unsigned char) ( (n) >> 16 );       \
    (b)[(i) + 2] = (unsigned char) ( (n) >>  8 );       \
    (b)[(i) + 3] = (unsigned char) ( (n)       );       \
}
#endif

int unhexify(unsigned char *obuf, const char *ibuf)
{
    unsigned char c, c2;
    int len = strlen(ibuf) / 2;
    assert(!(strlen(ibuf) %1)); // must be even number of bytes

    while (*ibuf != 0)
    {
        c = *ibuf++;
        if( c >= '0' && c <= '9' )
            c -= '0';
        else if( c >= 'a' && c <= 'f' )
            c -= 'a' - 10;
        else if( c >= 'A' && c <= 'F' )
            c -= 'A' - 10;
        else
            assert( 0 );

        c2 = *ibuf++;
        if( c2 >= '0' && c2 <= '9' )
            c2 -= '0';
        else if( c2 >= 'a' && c2 <= 'f' )
            c2 -= 'a' - 10;
        else if( c2 >= 'A' && c2 <= 'F' )
            c2 -= 'A' - 10;
        else
            assert( 0 );

        *obuf++ = ( c << 4 ) | c2;
    }

    return len;
}

void hexify(unsigned char *obuf, const unsigned char *ibuf, int len)
{
    unsigned char l, h;

    while (len != 0)
    {
        h = (*ibuf) / 16;
        l = (*ibuf) % 16;

        if( h < 10 )
            *obuf++ = '0' + h;
        else
            *obuf++ = 'a' + h - 10;

        if( l < 10 )
            *obuf++ = '0' + l;
        else
            *obuf++ = 'a' + l - 10;

        ++ibuf;
        len--;
    }
}

/**
 * This function just returns data from rand().
 * Although predictable and often similar on multiple
 * runs, this does not result in identical random on
 * each run. So do not use this if the results of a
 * test depend on the random data that is generated.
 *
 * rng_state shall be NULL.
 */
static int rnd_std_rand( void *rng_state, unsigned char *output, size_t len )
{
#if !defined(__OpenBSD__)
    size_t i;

    if( rng_state != NULL )
        rng_state  = NULL;

    for( i = 0; i < len; ++i )
        output[i] = rand();
#else
    if( rng_state != NULL )
        rng_state = NULL;

    arc4random_buf( output, len );
#endif /* !OpenBSD */

    return( 0 );
}

/**
 * This function only returns zeros
 *
 * rng_state shall be NULL.
 */
static int rnd_zero_rand( void *rng_state, unsigned char *output, size_t len )
{
    if( rng_state != NULL )
        rng_state  = NULL;

    memset( output, 0, len );

    return( 0 );
}

typedef struct
{
    unsigned char *buf;
    size_t length;
} rnd_buf_info;

/**
 * This function returns random based on a buffer it receives.
 *
 * rng_state shall be a pointer to a rnd_buf_info structure.
 * 
 * The number of bytes released from the buffer on each call to
 * the random function is specified by per_call. (Can be between
 * 1 and 4)
 *
 * After the buffer is empty it will return rand();
 */
static int rnd_buffer_rand( void *rng_state, unsigned char *output, size_t len )
{
    rnd_buf_info *info = (rnd_buf_info *) rng_state;
    size_t use_len;

    if( rng_state == NULL )
        return( rnd_std_rand( NULL, output, len ) );

    use_len = len;
    if( len > info->length )
        use_len = info->length;

    if( use_len )
    {
        memcpy( output, info->buf, use_len );
        info->buf += use_len;
        info->length -= use_len;
    }

    if( len - use_len > 0 )
        return( rnd_std_rand( NULL, output + use_len, len - use_len ) );

    return( 0 );
}

/**
 * Info structure for the pseudo random function
 *
 * Key should be set at the start to a test-unique value.
 * Do not forget endianness!
 * State( v0, v1 ) should be set to zero.
 */
typedef struct
{
    uint32_t key[16];
    uint32_t v0, v1;
} rnd_pseudo_info;

/**
 * This function returns random based on a pseudo random function.
 * This means the results should be identical on all systems.
 * Pseudo random is based on the XTEA encryption algorithm to
 * generate pseudorandom.
 *
 * rng_state shall be a pointer to a rnd_pseudo_info structure.
 */
static int rnd_pseudo_rand( void *rng_state, unsigned char *output, size_t len )
{
    rnd_pseudo_info *info = (rnd_pseudo_info *) rng_state;
    uint32_t i, *k, sum, delta=0x9E3779B9;
    unsigned char result[4], *out = output;

    if( rng_state == NULL )
        return( rnd_std_rand( NULL, output, len ) );

    k = info->key;

    while( len > 0 )
    {
        size_t use_len = ( len > 4 ) ? 4 : len;
        sum = 0;

        for( i = 0; i < 32; i++ )
        {
            info->v0 += (((info->v1 << 4) ^ (info->v1 >> 5)) + info->v1) ^ (sum + k[sum & 3]);
            sum += delta;
            info->v1 += (((info->v0 << 4) ^ (info->v0 >> 5)) + info->v0) ^ (sum + k[(sum>>11) & 3]);
        }

        PUT_UINT32_BE( info->v0, result, 0 );
        memcpy( out, result, use_len );
        len -= use_len;
        out += 4;
    }

    return( 0 );
}


FCT_BGN()
{
#ifdef POLARSSL_DHM_C
#ifdef POLARSSL_BIGNUM_C


    FCT_SUITE_BGN(test_suite_dhm)
    {

        FCT_TEST_BGN(diffie_hellman_full_exchange_1)
        {
            dhm_context ctx_srv;
            dhm_context ctx_cli;
            unsigned char ske[1000];
            unsigned char *p = ske;
            unsigned char pub_cli[1000];
            unsigned char sec_srv[1000];
            unsigned char sec_cli[1000];
            size_t ske_len = 0;
            size_t pub_cli_len = 0;
            size_t sec_srv_len = 1000;
            size_t sec_cli_len = 1000;
            int x_size;
            rnd_pseudo_info rnd_info;
        
            memset( &ctx_srv, 0x00, sizeof( dhm_context ) );
            memset( &ctx_cli, 0x00, sizeof( dhm_context ) );
            memset( ske, 0x00, 1000 );
            memset( pub_cli, 0x00, 1000 );
            memset( sec_srv, 0x00, 1000 );
            memset( sec_cli, 0x00, 1000 );
            memset( &rnd_info, 0x00, sizeof( rnd_pseudo_info ) );
        
            fct_chk( mpi_read_string( &ctx_srv.P, 10, "23" ) == 0 );
            fct_chk( mpi_read_string( &ctx_srv.G, 10, "5" ) == 0 );
            x_size = mpi_size( &ctx_srv.P );
        
            fct_chk( dhm_make_params( &ctx_srv, x_size, ske, &ske_len, &rnd_pseudo_rand, &rnd_info ) == 0 );
            ske[ske_len++] = 0;
            ske[ske_len++] = 0;
            fct_chk( dhm_read_params( &ctx_cli, &p, ske + ske_len ) == 0 );
        
            pub_cli_len = x_size;
            fct_chk( dhm_make_public( &ctx_cli, x_size, pub_cli, pub_cli_len, &rnd_pseudo_rand, &rnd_info ) == 0 );
        
            fct_chk( dhm_read_public( &ctx_srv, pub_cli, pub_cli_len ) == 0 );
        
            fct_chk( dhm_calc_secret( &ctx_srv, sec_srv, &sec_srv_len ) == 0 );
            fct_chk( dhm_calc_secret( &ctx_cli, sec_cli, &sec_cli_len ) == 0 );
        
            fct_chk( sec_srv_len == sec_cli_len );
            fct_chk( sec_srv_len != 0 );
            fct_chk( memcmp( sec_srv, sec_cli, sec_srv_len ) == 0 );
        
            dhm_free( &ctx_srv );
            dhm_free( &ctx_cli );
        }
        FCT_TEST_END();


        FCT_TEST_BGN(diffie_hellman_full_exchange_2)
        {
            dhm_context ctx_srv;
            dhm_context ctx_cli;
            unsigned char ske[1000];
            unsigned char *p = ske;
            unsigned char pub_cli[1000];
            unsigned char sec_srv[1000];
            unsigned char sec_cli[1000];
            size_t ske_len = 0;
            size_t pub_cli_len = 0;
            size_t sec_srv_len = 1000;
            size_t sec_cli_len = 1000;
            int x_size;
            rnd_pseudo_info rnd_info;
        
            memset( &ctx_srv, 0x00, sizeof( dhm_context ) );
            memset( &ctx_cli, 0x00, sizeof( dhm_context ) );
            memset( ske, 0x00, 1000 );
            memset( pub_cli, 0x00, 1000 );
            memset( sec_srv, 0x00, 1000 );
            memset( sec_cli, 0x00, 1000 );
            memset( &rnd_info, 0x00, sizeof( rnd_pseudo_info ) );
        
            fct_chk( mpi_read_string( &ctx_srv.P, 10, "93450983094850938450983409623" ) == 0 );
            fct_chk( mpi_read_string( &ctx_srv.G, 10, "9345098304850938450983409622" ) == 0 );
            x_size = mpi_size( &ctx_srv.P );
        
            fct_chk( dhm_make_params( &ctx_srv, x_size, ske, &ske_len, &rnd_pseudo_rand, &rnd_info ) == 0 );
            ske[ske_len++] = 0;
            ske[ske_len++] = 0;
            fct_chk( dhm_read_params( &ctx_cli, &p, ske + ske_len ) == 0 );
        
            pub_cli_len = x_size;
            fct_chk( dhm_make_public( &ctx_cli, x_size, pub_cli, pub_cli_len, &rnd_pseudo_rand, &rnd_info ) == 0 );
        
            fct_chk( dhm_read_public( &ctx_srv, pub_cli, pub_cli_len ) == 0 );
        
            fct_chk( dhm_calc_secret( &ctx_srv, sec_srv, &sec_srv_len ) == 0 );
            fct_chk( dhm_calc_secret( &ctx_cli, sec_cli, &sec_cli_len ) == 0 );
        
            fct_chk( sec_srv_len == sec_cli_len );
            fct_chk( sec_srv_len != 0 );
            fct_chk( memcmp( sec_srv, sec_cli, sec_srv_len ) == 0 );
        
            dhm_free( &ctx_srv );
            dhm_free( &ctx_cli );
        }
        FCT_TEST_END();


        FCT_TEST_BGN(diffie_hellman_full_exchange_2)
        {
            dhm_context ctx_srv;
            dhm_context ctx_cli;
            unsigned char ske[1000];
            unsigned char *p = ske;
            unsigned char pub_cli[1000];
            unsigned char sec_srv[1000];
            unsigned char sec_cli[1000];
            size_t ske_len = 0;
            size_t pub_cli_len = 0;
            size_t sec_srv_len = 1000;
            size_t sec_cli_len = 1000;
            int x_size;
            rnd_pseudo_info rnd_info;
        
            memset( &ctx_srv, 0x00, sizeof( dhm_context ) );
            memset( &ctx_cli, 0x00, sizeof( dhm_context ) );
            memset( ske, 0x00, 1000 );
            memset( pub_cli, 0x00, 1000 );
            memset( sec_srv, 0x00, 1000 );
            memset( sec_cli, 0x00, 1000 );
            memset( &rnd_info, 0x00, sizeof( rnd_pseudo_info ) );
        
            fct_chk( mpi_read_string( &ctx_srv.P, 10, "93450983094850938450983409623982317398171298719873918739182739712938719287391879381271" ) == 0 );
            fct_chk( mpi_read_string( &ctx_srv.G, 10, "9345098309485093845098340962223981329819812792137312973297123912791271" ) == 0 );
            x_size = mpi_size( &ctx_srv.P );
        
            fct_chk( dhm_make_params( &ctx_srv, x_size, ske, &ske_len, &rnd_pseudo_rand, &rnd_info ) == 0 );
            ske[ske_len++] = 0;
            ske[ske_len++] = 0;
            fct_chk( dhm_read_params( &ctx_cli, &p, ske + ske_len ) == 0 );
        
            pub_cli_len = x_size;
            fct_chk( dhm_make_public( &ctx_cli, x_size, pub_cli, pub_cli_len, &rnd_pseudo_rand, &rnd_info ) == 0 );
        
            fct_chk( dhm_read_public( &ctx_srv, pub_cli, pub_cli_len ) == 0 );
        
            fct_chk( dhm_calc_secret( &ctx_srv, sec_srv, &sec_srv_len ) == 0 );
            fct_chk( dhm_calc_secret( &ctx_cli, sec_cli, &sec_cli_len ) == 0 );
        
            fct_chk( sec_srv_len == sec_cli_len );
            fct_chk( sec_srv_len != 0 );
            fct_chk( memcmp( sec_srv, sec_cli, sec_srv_len ) == 0 );
        
            dhm_free( &ctx_srv );
            dhm_free( &ctx_cli );
        }
        FCT_TEST_END();

    }
    FCT_SUITE_END();

#endif /* POLARSSL_DHM_C */
#endif /* POLARSSL_BIGNUM_C */

}
FCT_END();

