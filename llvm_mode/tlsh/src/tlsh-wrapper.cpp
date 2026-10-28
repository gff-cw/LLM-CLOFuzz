#include "tlsh-wrapper.h"
#include "tlsh.h"

extern "C" Tlsh* Tlsh_new()
{
        return new Tlsh;
}

extern "C" const char* Tlsh_get_hash_buffer(Tlsh* tlsh, char *buffer, unsigned int bufSize, int showvers)
{
        return tlsh->getHash(buffer, bufSize, showvers);
}

extern "C" const char* Tlsh_get_hash(Tlsh* tlsh, int showvers)
{
        return tlsh->getHash(showvers);
}

extern "C" void Tlsh_update(Tlsh* tlsh, const unsigned char* data, unsigned int len)
{
        tlsh->update(data, len);
}

extern "C" void Tlsh_final(Tlsh* tlsh, const unsigned char* data, unsigned int len, int fc_cons_option)
{
        tlsh->final(data, len, fc_cons_option);
}

extern "C" int Tlsh_from_str(Tlsh* tlsh, const char* str)
{
        tlsh->fromTlshStr(str);
}

extern "C" int Tlsh_total_diff(Tlsh* tlsh, const Tlsh *other, int len_diff)
{
        return tlsh->totalDiff(other, len_diff);
}

extern "C" void Tlsh_reset(Tlsh* tlsh)
{
        tlsh->reset();
}

extern "C" void Tlsh_delete(Tlsh* tlsh)
{
        delete tlsh;
}

