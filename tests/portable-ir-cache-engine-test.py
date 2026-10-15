#!/usr/bin/env python3
"""Controller contract checks; these are never native/guest cache acceptance."""
import importlib.util
from pathlib import Path
import struct
import unittest

spec=importlib.util.spec_from_file_location('engine_probe',Path(__file__).with_name('portable-ir-cache-engine.py'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

class Tests(unittest.TestCase):
    def rows(self):
        result={}
        for n,name in enumerate(['off','cold','reuse','different_cpu_mode','changed_guest_bytes']):
            result[name]={'status':'PASS','processRetired':True,'identity':{'pid':10+n,'startTicks':str(n),'hostBootId':'test'},'engineSHA256':'a'*64,'cacheEnabledWitness':name!='off','cacheCounters':None if name=='off' else {'hits':1 if name=='reuse' else 0,'stored':0 if name=='reuse' else 2,'misses':2,'rejected':0}}
        return result
    def test_matrix(self):
        self.assertEqual(m.validate_matrix(self.rows())['status'],'PASS')
    def test_fresh_process_required(self):
        rows=self.rows();rows['reuse']['identity']=rows['cold']['identity']
        with self.assertRaises(ValueError):m.validate_matrix(rows)
    def test_real_hit_required(self):
        rows=self.rows();rows['reuse']['cacheCounters']['hits']=0
        with self.assertRaises(ValueError):m.validate_matrix(rows)
    def test_off_witness_rejected(self):
        rows=self.rows();rows['off']['cacheEnabledWitness']=True
        with self.assertRaises(ValueError):m.validate_matrix(rows)
    def test_different_context_cannot_hit(self):
        rows=self.rows();rows['different_cpu_mode']['cacheCounters']['hits']=1
        with self.assertRaises(ValueError):m.validate_matrix(rows)
    def test_mixed_engine_rejected(self):
        rows=self.rows();rows['reuse']['engineSHA256']='b'*64
        with self.assertRaises(ValueError):m.validate_matrix(rows)
    def test_counts_monotonic(self):
        prefix='LightTouch TCG IR cache: '
        suffix='; frontend IR only, fresh native code\n'
        good=prefix+'hits=1 misses=2 rejected=0 stored=3'+suffix
        self.assertEqual(m.counters(good)['stored'],3)
        with self.assertRaises(ValueError):m.counters(good+prefix+'hits=0 misses=2 rejected=0 stored=3'+suffix)
    def test_legal_fixture(self):
        words=struct.unpack('<5I',m.words())
        branch=0x1010+8+((words[4]&0xffffff)<<2)
        self.assertEqual(branch,0x2000)
        self.assertEqual(struct.unpack('<5I',m.words(True))[1],0xe0400001)

if __name__=='__main__':unittest.main()
