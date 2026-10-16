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
        for n,name in enumerate(['off','cold','reuse','different_cpu_mode','changed_guest_bytes','self_modifying_code']):
            result[name]={'status':'PASS','processRetired':True,'identity':{'pid':10+n,'startTicks':str(n),'hostBootId':'test'},'engineSHA256':'a'*64,'cacheEnabledWitness':name!='off','selfModify':name=='self_modifying_code','cacheCounters':None if name=='off' else {'hits':2 if name in ['reuse','self_modifying_code'] else 0,'stored':0 if name=='reuse' else 2,'misses':2,'rejected':0}}
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
        branch=m.CODE_PC+16+8+((words[4]&0xffffff)<<2)
        self.assertEqual(branch,m.TARGET_PC)
        self.assertEqual(m.CODE_PC//1024,m.TARGET_PC//1024)
        self.assertNotEqual(m.CODE_PC%1024,0)
        self.assertEqual(struct.unpack('<5I',m.words(True))[1],0xe0400001)
    def test_delayed_first_execution_is_observed_without_stepping(self):
        class QMP:
            def __init__(self):self.commands=[]
            def command(self,name):self.commands.append(name)
        q=QMP();now=[0];states=iter([{'pc':m.CODE_PC},{'pc':m.CODE_PC},{'pc':m.TARGET_PC}])
        def sleep(delay):now[0]+=delay
        actual,observed=m.run_until_target(q,lambda:next(states),clock=lambda:now[0],sleep=sleep)
        self.assertEqual(actual['pc'],m.TARGET_PC)
        self.assertEqual(len(observed),3)
        self.assertEqual(q.commands,['cont','stop']*3)
    def test_unexecuted_guest_cannot_pass(self):
        class QMP:
            def command(self,name):pass
        now=[0]
        def sleep(delay):now[0]+=1
        with self.assertRaisesRegex(TimeoutError,'did not reach target'):
            m.run_until_target(QMP(),lambda:{'pc':m.CODE_PC},clock=lambda:now[0],sleep=sleep)
    def test_loop_only_hits_do_not_qualify_arithmetic_and_memory_replay(self):
        rows=self.rows();rows['reuse']['cacheCounters']['hits']=1
        with self.assertRaises(ValueError):m.validate_matrix(rows)
    def test_missing_self_modification_witness_is_rejected(self):
        rows=self.rows();rows['self_modifying_code']['selfModify']=False
        with self.assertRaises(ValueError):m.validate_matrix(rows)

if __name__=='__main__':unittest.main()
