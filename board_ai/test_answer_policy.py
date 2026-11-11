import unittest
from answer_policy import constrain,visible_length

class AnswerPolicyTests(unittest.TestCase):
    def test_prompt_and_budget_are_independent(self):
        source=dict(mode='text',question='列出巡检重点',answer_limit=96,max_tokens=96)
        prepared=constrain(source)
        self.assertEqual(source['question'],'列出巡检重点')
        self.assertIn('不得超过96字',prepared['question'])
        self.assertGreater(prepared['max_tokens'],source['answer_limit']*10)

    def test_vision_uses_same_constraint(self):
        p=constrain(dict(mode='vision',question='描述现场',answer_limit=256,path='/tmp/image.jpg'))
        self.assertIn('不得超过256字',p['question'])
        self.assertEqual(p['path'],'/tmp/image.jpg')

    def test_asr_is_not_limited_or_rewritten(self):
        p=dict(mode='asr',question='',answer_limit=96,max_tokens=4096)
        self.assertEqual(constrain(p),p)

    def test_retry_requires_more_concise_complete_answer(self):
        p=dict(mode='text',question='问题',answer_limit=100)
        self.assertIn('65字以内',constrain(p,1)['question'])
        self.assertIn('45字以内',constrain(p,2)['question'])
        self.assertIn('完整答案',constrain(p,2)['question'])

    def test_count_excludes_markdown_and_link_target(self):
        self.assertEqual(visible_length('# 标题\n\n**正常**，见[资料](https://example.com/a/very/long/path)。'),9)

if __name__=='__main__':unittest.main()
